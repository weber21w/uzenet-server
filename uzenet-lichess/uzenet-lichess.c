#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <fcntl.h>
#include <syslog.h>
#include <pthread.h>
#include <curl/curl.h>

#include "uzenet-lichess.h"

/* Paths / config */

#define LICHESS_SOCK_PATH	"/run/uzenet/lichess.sock"
#define LICHESS_USERS_DIR	"/var/lib/uzenet/lichess-users"

/* Tunnel framing (UzeNet-room <-> service)
 *
 * 4-byte header:
 *   [0] = type
 *   [1] = flags
 *   [2] = length hi
 *   [3] = length lo
 * Followed by "length" bytes of payload.
 */

#define TUNNEL_TYPE_LOGIN	0x01
#define TUNNEL_TYPE_DATA	0x02

#define TUNNEL_MAX_PAYLOAD	64

typedef struct{
	u8	type;
	u8	flags;
	u16	length;
	u8	data[TUNNEL_MAX_PAYLOAD];
} TunnelFrame;

typedef struct{
	u16	user_id;
	u16	reserved;
} TunnelLoginMeta;

/* Client state */

#define LCH_MAX_CLIENTS			64
#define LCH_OUTQ_SLOTS			16	/* power-of-two */
#define LCH_OUTQ_SLOT_BYTES		64	/* must be <= TUNNEL_MAX_PAYLOAD */

#define LCH_MAX_MOVES			512	/* half-moves (plies) */
#define LCH_MAX_CHAT_LINES		256

typedef enum{
	CLST_UNUSED = 0,
	CLST_ACTIVE = 1,
} client_state_t;

typedef struct{
	u8	from_sq;
	u8	to_sq;
	u8	promo;
} MoveLogEntry;

typedef struct{
	u8	len;
	char text[LCH_CHAT_MAX_LEN];
} ChatLogEntry;

typedef struct{
	int		has_token;
	char	token[128];
} LichessPrefs;

typedef struct{
	int				fd;
	client_state_t	state;

	u16				user_id;
	LichessPrefs	prefs;
	int				prefs_loaded;

	char			game_id[16];
	int				in_game;
	int				my_side;		/* LCH_SIDE_*, or -1 unknown */

	u16				white_secs;
	u16				black_secs;

	/* last move we sent to Lichess (UCI), to filter from stream */
	char			last_sent_uci[6];
	int				last_sent_valid;

	/* move / chat logs (server-side only) */
	u16				move_count;
	MoveLogEntry	moves[LCH_MAX_MOVES];

	u16				chat_count;
	ChatLogEntry	chat[LCH_MAX_CHAT_LINES];

	/* outgoing queue to Uzebox (LCH_* payloads) */
	u8				outq[LCH_OUTQ_SLOTS][LCH_OUTQ_SLOT_BYTES];
	u8				outq_len[LCH_OUTQ_SLOTS];
	u8				outq_head;
	u8				outq_tail;
	pthread_mutex_t	outq_mutex;

	pthread_t		stream_tid;
	int				stream_running;
	int				stream_quit;
} client_t;

static client_t g_clients[LCH_MAX_CLIENTS];

static int g_listen_fd = -1;
static int g_running = 1;

/* Helpers for JSON/NDJSON parsing from game stream */

typedef struct{
	int		has_move;
	char	last_uci[6];

	int		has_wtime, has_btime;
	int		wtime_ms, btime_ms;

	int		has_status;
	char	status[16];

	int		ply_count;	/* half-move count from moves[] */
} GameStateInfo;

/* --------------------------------------------------------------------- */
/* small utils                                                           */
/* --------------------------------------------------------------------- */

static void set_nonblock(int fd){
	int flags = fcntl(fd, F_GETFL, 0);
	if(flags < 0) return;
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void on_sigint(int sig){
	(void)sig;
	g_running = 0;
}

static int read_full(int fd, void *buf, size_t len){
	u8 *p = (u8*)buf;
	while(len){
		ssize_t r = read(fd, p, len);
		if(r == 0) return 0;
		if(r < 0){
			if(errno == EINTR) continue;
			return -1;
		}
		p   += r;
		len -= r;
	}
	return 1;
}

static int write_full(int fd, const void *buf, size_t len){
	const u8 *p = (const u8*)buf;
	while(len){
		ssize_t r = write(fd, p, len);
		if(r < 0){
			if(errno == EINTR) continue;
			return -1;
		}
		p   += r;
		len -= r;
	}
	return 0;
}

/* --------------------------------------------------------------------- */
/* libcurl buffer helper                                                 */
/* --------------------------------------------------------------------- */

typedef struct{
	char *buf;
	size_t cap;
	size_t len;
} LCH_Buffer;

static size_t lch_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata){
	LCH_Buffer *b = (LCH_Buffer*)userdata;
	size_t got = size * nmemb;
	size_t avail = (b->cap > b->len) ? (b->cap - b->len) : 0;

	if(avail && got){
		if(got > avail) got = avail;
		memcpy(b->buf + b->len, ptr, got);
		b->len += got;
	}

	/* tell libcurl we "handled" everything it gave us */
	return size * nmemb;
}

/* minimal POST helper: application/x-www-form-urlencoded -> JSON string */
static int lch_http_post_form(const char *token,
	const char *path,
	const char *form_body,
	char *out,
	size_t out_sz)
{
	CURL *curl;
	CURLcode res;
	LCH_Buffer buf;
	struct curl_slist *hdrs = NULL;
	char url[128];
	char auth[256];

	if(!out || !out_sz) return -1;
	out[0] = 0;

	curl = curl_easy_init();
	if(!curl) return -1;

	snprintf(url, sizeof(url), "https://lichess.org%s", path);

	hdrs = curl_slist_append(hdrs, "Accept: application/json");
	hdrs = curl_slist_append(hdrs, "Content-Type: application/x-www-form-urlencoded");

	if(token && token[0]){
		snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);
		hdrs = curl_slist_append(hdrs, auth);
	}

	buf.buf = out;
	buf.cap = out_sz - 1;
	buf.len = 0;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form_body);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, lch_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "uzenet-lichess/0.1");

	res = curl_easy_perform(curl);

	curl_slist_free_all(hdrs);
	curl_easy_cleanup(curl);

	if(res != CURLE_OK){
		return -1;
	}

	out[buf.len] = 0;
	return (int)buf.len;
}

/* Very small JSON scraper: find challenge.id inside the response */
static int lch_extract_challenge_id(const char *json,
	size_t len,
	char *out,
	size_t out_sz)
{
	const char *p;
	const char *end;
	const char *limit;

	if(!json || !out || !out_sz) return -1;

	/* narrow search to the "challenge" object first */
	p = strstr(json, "\"challenge\"");
	if(!p) return -1;

	p = strstr(p, "\"id\":\"");
	if(!p) return -1;

	p += strlen("\"id\":\"");
	limit = json + len;
	end = p;

	while(end < limit && *end && *end != '"'){
		end++;
	}

	if(end <= p) return -1;

	len = (size_t)(end - p);
	if(len >= out_sz) len = out_sz - 1;

	memcpy(out, p, len);
	out[len] = 0;

	return 0;
}

/* --------------------------------------------------------------------- */
/* Tunnel framing I/O                                                    */
/* --------------------------------------------------------------------- */

static int ReadTunnelFramed(int fd, TunnelFrame *fr){
	u8 hdr[4];
	int r = read_full(fd, hdr, sizeof(hdr));
	if(r <= 0) return r;

	fr->type   = hdr[0];
	fr->flags  = hdr[1];
	fr->length = (u16)((hdr[2] << 8) | hdr[3]);
	if(fr->length > TUNNEL_MAX_PAYLOAD) return -1;

	if(fr->length){
		r = read_full(fd, fr->data, fr->length);
		if(r <= 0) return r;
	}
	return 1;
}

static int WriteTunnelFramed(int fd, const TunnelFrame *fr){
	u8 hdr[4];
	u16 len = fr->length;
	if(len > TUNNEL_MAX_PAYLOAD) len = TUNNEL_MAX_PAYLOAD;

	hdr[0] = fr->type;
	hdr[1] = fr->flags;
	hdr[2] = (u8)(len >> 8);
	hdr[3] = (u8)(len & 0xff);

	if(write_full(fd, hdr, sizeof(hdr)) < 0) return -1;
	if(len){
		if(write_full(fd, fr->data, len) < 0) return -1;
	}
	return 0;
}

/* --------------------------------------------------------------------- */
/* Ring buffer helpers                                                   */
/* --------------------------------------------------------------------- */

static int lch_enqueue_to_client(client_t *c, const void *msg, u8 len){
	u8 next;

	if(!c) return -1;
	pthread_mutex_lock(&c->outq_mutex);
	next = (u8)((c->outq_tail + 1U) & (LCH_OUTQ_SLOTS - 1U));
	if(next == c->outq_head){
		/* queue full */
		pthread_mutex_unlock(&c->outq_mutex);
		return -2;
	}
	if(len > LCH_OUTQ_SLOT_BYTES) len = LCH_OUTQ_SLOT_BYTES;
	memcpy(c->outq[c->outq_tail], msg, len);
	c->outq_len[c->outq_tail] = len;
	c->outq_tail = next;
	pthread_mutex_unlock(&c->outq_mutex);
	return 0;
}

static int lch_dequeue_from_client(client_t *c, void *buf, u8 *len){
	u8 idx;

	if(!c || !buf || !len) return -1;
	pthread_mutex_lock(&c->outq_mutex);
	if(c->outq_head == c->outq_tail){
		pthread_mutex_unlock(&c->outq_mutex);
		return -2;	/* empty */
	}
	idx  = c->outq_head;
	*len = c->outq_len[idx];
	memcpy(buf, c->outq[idx], *len);
	c->outq_head = (u8)((idx + 1U) & (LCH_OUTQ_SLOTS - 1U));
	pthread_mutex_unlock(&c->outq_mutex);
	return 0;
}

/* --------------------------------------------------------------------- */
/* JSON scrapers for gameState / chatLine NDJSON                         */
/* --------------------------------------------------------------------- */

static int parse_int_after(const char *s, const char *tag, int *out){
	const char *p = strstr(s, tag);
	int v = 0;

	if(!p) return -1;
	p += (int)strlen(tag);
	while(*p == ' ' || *p == ':') p++;
	if(*p < '0' || *p > '9') return -1;
	while(*p >= '0' && *p <= '9'){
		v = v * 10 + (*p - '0');
		p++;
	}
	*out = v;
	return 0;
}

static int parse_string_after(const char *s, const char *tag, char *out, size_t out_sz){
	const char *p = strstr(s, tag);
	size_t i = 0;

	if(!p) return -1;
	p += strlen(tag);
	while(*p && *p != '"') p++;
	if(*p != '"') return -1;
	p++;
	while(*p && *p != '"' && i < out_sz - 1){
		out[i++] = *p++;
	}
	out[i] = '\0';
	return 0;
}

static void lch_parse_game_state_line(const char *line, GameStateInfo *gs){
	const char *pmoves, *p, *last_space;
	size_t len;

	if(!gs){
		return;
	}
	memset(gs, 0, sizeof(*gs));

	/* moves */
	pmoves = strstr(line, "\"moves\":\"");
	if(pmoves){
		int ply = 0;
		pmoves += 9;	/* skip "moves":" */
		p = pmoves;
		last_space = NULL;

		while(*p && *p != '"'){
			if(*p == ' '){
				last_space = p;
				ply++;
			}
			p++;
		}
		/* if there was at least one move, total plies = spaces + 1 */
		if(p > pmoves){
			ply++;
		}
		gs->ply_count = ply;

		if(p > pmoves){
			const char *start;
			if(last_space){
				start = last_space + 1;
			}else{
				start = pmoves;
			}
			len = (size_t)(p - start);
			if(len > 5) len = 5;
			memcpy(gs->last_uci, start, len);
			gs->last_uci[len] = '\0';
			gs->has_move = 1;
		}
	}

	/* wtime / btime */
	if(parse_int_after(line, "\"wtime\":", &gs->wtime_ms) == 0){
		gs->has_wtime = 1;
	}
	if(parse_int_after(line, "\"btime\":", &gs->btime_ms) == 0){
		gs->has_btime = 1;
	}

	/* status */
	if(parse_string_after(line, "\"status\":\"", gs->status, sizeof(gs->status)) == 0){
		gs->has_status = 1;
	}
}

static u8 lch_map_status_to_result(const char *status, int my_side){
	(void)my_side;
	if(!status) return LCH_RESULT_UNKNOWN;

	if(strcmp(status, "draw") == 0 ||
	   strcmp(status, "stalemate") == 0){
		return LCH_RESULT_DRAW;
	}
	if(strcmp(status, "aborted") == 0){
		return LCH_RESULT_ABORTED;
	}
	/* mate/resign/timeout: we don't know winner here without "winner" field */
	if(strcmp(status, "mate") == 0 ||
	   strcmp(status, "resign") == 0 ||
	   strcmp(status, "timeout") == 0){
		return LCH_RESULT_UNKNOWN;
	}
	return LCH_RESULT_UNKNOWN;
}

static u8 lch_map_status_to_reason(const char *status){
	if(!status) return LCH_REASON_NONE;

	if(strcmp(status, "mate") == 0)		return LCH_REASON_CHECKMATE;
	if(strcmp(status, "resign") == 0)	return LCH_REASON_RESIGN;
	if(strcmp(status, "timeout") == 0)	return LCH_REASON_TIMEOUT;
	if(strcmp(status, "stalemate") == 0)return LCH_REASON_STALEMATE;
	if(strcmp(status, "draw") == 0)		return LCH_REASON_AGREED_DRAW;
	if(strcmp(status, "aborted") == 0)	return LCH_REASON_ABORTED;

	return LCH_REASON_NONE;
}

/* --------------------------------------------------------------------- */
/* prefs / token loading                                                 */
/* --------------------------------------------------------------------- */

static int load_user_token(u16 user_id, LichessPrefs *out){
	char path[256];
	char buf[1024];
	FILE *f;
	size_t n;

	snprintf(path, sizeof(path), "%s/%u.json", LICHESS_USERS_DIR, (unsigned)user_id);
	f = fopen(path, "r");
	if(!f) return -1;

	n = fread(buf, 1, sizeof(buf)-1, f);
	fclose(f);
	if(n == 0) return -1;
	buf[n] = '\0';

	/* naive: look for "token":"... */
	char *p = strstr(buf, "\"token\"");
	if(!p) return -1;
	p = strchr(p, ':');
	if(!p) return -1;
	p++;
	while(*p == ' ' || *p == '\t') p++;
	if(*p != '"' && *p != '\'') return -1;
	char quote = *p;
	p++;

	char *q = p;
	while(*q && *q != quote && (q - p) < (int)(sizeof(out->token)-1)) q++;

	memset(out->token, 0, sizeof(out->token));
	memcpy(out->token, p, (size_t)(q - p));
	out->token[q - p] = '\0';
	out->has_token = 1;
	return 0;
}

static void load_prefs_for_client(client_t *c){
	const char *shared;

	if(c->prefs_loaded) return;

	memset(&c->prefs, 0, sizeof(c->prefs));

	if(c->user_id != 0xffff){
		if(load_user_token(c->user_id, &c->prefs) == 0){
			c->prefs_loaded = 1;
			return;
		}
	}

	shared = getenv("LICHESS_SHARED_TOKEN");
	if(shared && *shared){
		strncpy(c->prefs.token, shared, sizeof(c->prefs.token)-1);
		c->prefs.token[sizeof(c->prefs.token)-1] = '\0';
		c->prefs.has_token = 1;
		c->prefs_loaded = 1;
		return;
	}
}

static const char *get_token_for_client(client_t *c){
	load_prefs_for_client(c);
	if(c->prefs.has_token && c->prefs.token[0]){
		return c->prefs.token;
	}
	return NULL;
}

/* --------------------------------------------------------------------- */
/* Lichess HTTP helpers                                                  */
/* --------------------------------------------------------------------- */

static int lichess_init(void){
	return curl_global_init(CURL_GLOBAL_DEFAULT);
}

static void lichess_shutdown(void){
	curl_global_cleanup();
}

int lichess_start_random_game(const char *token,
	int rated,
	unsigned clock_limit,
	unsigned clock_increment,
	char *out_game_id,
	size_t out_game_id_sz)
{
	char body[128];
	char resp[512];
	int n;

	if(!token || !token[0]) return -1;
	if(!out_game_id || !out_game_id_sz) return -1;

	/* /api/challenge/open, standard variant, random color */
	snprintf(body, sizeof(body),
		"rated=%s&clock.limit=%u&clock.increment=%u&variant=standard&color=random",
		rated ? "true" : "false",
		clock_limit,
		clock_increment);

	n = lch_http_post_form(token, "/api/challenge/open", body, resp, sizeof(resp));
	if(n < 0){
		return -1;
	}

	if(lch_extract_challenge_id(resp, (size_t)n, out_game_id, out_game_id_sz) != 0){
		return -1;
	}

	/* For open challenges, challenge ID == game ID once paired.
	 * You can now immediately open /api/board/game/stream/{out_game_id} from here.
	 */

	return 0;
}

static int lichess_post_no_body(const char *token, const char *url){
	CURL *curl;
	CURLcode res;
	struct curl_slist *hdr = NULL;
	char auth[256];

	curl = curl_easy_init();
	if(!curl) return -1;

	snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);
	hdr = curl_slist_append(hdr, auth);

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "uzenet-lichess/0.1");
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

	res = curl_easy_perform(curl);

	curl_slist_free_all(hdr);
	curl_easy_cleanup(curl);

	return (res == CURLE_OK) ? 0 : -1;
}

static int lichess_send_move(const char *token, const char *game_id, const char *uci, int offer_draw){
	char url[256];

	(void)offer_draw; /* could be mapped to an HTTP header if needed */

	snprintf(url, sizeof(url), "https://lichess.org/api/board/game/%s/move/%s", game_id, uci);
	return lichess_post_no_body(token, url);
}

static int lichess_resign_game(const char *token, const char *game_id){
	char url[256];
	snprintf(url, sizeof(url), "https://lichess.org/api/board/game/%s/resign", game_id);
	return lichess_post_no_body(token, url);
}

static int lichess_abort_game(const char *token, const char *game_id){
	char url[256];
	snprintf(url, sizeof(url), "https://lichess.org/api/board/game/%s/abort", game_id);
	return lichess_post_no_body(token, url);
}

static int lichess_send_chat(const char *token, const char *game_id, const char *text){
	CURL *curl;
	CURLcode res;
	struct curl_slist *hdr = NULL;
	char auth[256];
	char url[256];
	char *enc = NULL;
	char post[256];

	if(!text || !text[0]) return 0;

	curl = curl_easy_init();
	if(!curl) return -1;

	snprintf(url, sizeof(url), "https://lichess.org/api/board/game/%s/chat", game_id);
	snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);
	hdr = curl_slist_append(hdr, auth);
	hdr = curl_slist_append(hdr, "Content-Type: application/x-www-form-urlencoded");

	enc = curl_easy_escape(curl, text, 0);
	if(!enc){
		curl_slist_free_all(hdr);
		curl_easy_cleanup(curl);
		return -1;
	}

	snprintf(post, sizeof(post), "room=player&text=%s", enc);
	curl_free(enc);

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "uzenet-lichess/0.1");
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

	res = curl_easy_perform(curl);

	curl_slist_free_all(hdr);
	curl_easy_cleanup(curl);

	return (res == CURLE_OK) ? 0 : -1;
}

/* Streaming game NDJSON */

typedef void (*LichessStreamCB)(const char *line, void *userdata);

typedef struct{
	LichessStreamCB	cb;
	void			*userdata;
	char			buf[4096];
	size_t			len;
} StreamCtx;

static size_t stream_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata){
	size_t total = size * nmemb;
	StreamCtx *ctx = (StreamCtx*)userdata;
	size_t i;

	for(i=0;i<total;i++){
		char ch = ptr[i];
		if(ch == '\n' || ch == '\r'){
			if(ctx->len > 0 && ctx->cb){
				ctx->buf[ctx->len] = '\0';
				ctx->cb(ctx->buf, ctx->userdata);
				ctx->len = 0;
			}
		}else{
			if(ctx->len < sizeof(ctx->buf)-1){
				ctx->buf[ctx->len++] = ch;
			}
		}
	}
	return total;
}

static int lichess_stream_game(const char *token, const char *game_id, LichessStreamCB cb, void *userdata){
	CURL *curl;
	CURLcode res;
	struct curl_slist *hdr = NULL;
	char auth[256];
	char url[256];
	StreamCtx ctx;

	memset(&ctx, 0, sizeof(ctx));
	ctx.cb = cb;
	ctx.userdata = userdata;

	snprintf(url, sizeof(url), "https://lichess.org/api/board/game/stream/%s", game_id);
	snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);

	curl = curl_easy_init();
	if(!curl) return -1;

	hdr = curl_slist_append(hdr, auth);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "uzenet-lichess/0.1");
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);	/* streaming, rely on server close */
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

	res = curl_easy_perform(curl);

	if(ctx.len > 0 && cb){
		ctx.buf[ctx.len] = '\0';
		cb(ctx.buf, userdata);
	}

	curl_slist_free_all(hdr);
	curl_easy_cleanup(curl);

	return (res == CURLE_OK) ? 0 : -1;
}

/* --------------------------------------------------------------------- */
/* client lifecycle                                                      */
/* --------------------------------------------------------------------- */

static client_t *alloc_client(int fd){
	int i;

	for(i=0;i<LCH_MAX_CLIENTS;i++){
		client_t *c = &g_clients[i];
		if(c->state == CLST_UNUSED){
			memset(c, 0, sizeof(*c));
			c->fd       = fd;
			c->state    = CLST_ACTIVE;
			c->user_id  = 0xffff;
			c->my_side  = -1;
			c->move_count = 0;
			c->chat_count = 0;
			pthread_mutex_init(&c->outq_mutex, NULL);
			return c;
		}
	}
	return NULL;
}

static void free_client(client_t *c){
	if(!c) return;

	if(c->stream_running){
		c->stream_quit = 1;
		/* NOTE: we don't signal libcurl from here; join may block
		   until stream ends. Good enough for now. */
		pthread_join(c->stream_tid, NULL);
	}
	close(c->fd);
	pthread_mutex_destroy(&c->outq_mutex);
	memset(c, 0, sizeof(*c));
	c->state = CLST_UNUSED;
}

/* --------------------------------------------------------------------- */
/* LCH msg sender over tunnel                                            */
/* --------------------------------------------------------------------- */

static int send_lch_msg(client_t *c, const void *msg, u8 len){
	TunnelFrame fr;

	if(len > TUNNEL_MAX_PAYLOAD) len = TUNNEL_MAX_PAYLOAD;
	fr.type   = TUNNEL_TYPE_DATA;
	fr.flags  = 0;
	fr.length = len;
	memcpy(fr.data, msg, len);

	if(WriteTunnelFramed(c->fd, &fr) < 0){
		return -1;
	}
	return 0;
}

/* --------------------------------------------------------------------- */
/* stream thread & callback                                              */
/* --------------------------------------------------------------------- */

static void append_chat_line(client_t *c, const char *line){
	if(!line) return;
	if(c->chat_count >= LCH_MAX_CHAT_LINES) return;

	ChatLogEntry *ce = &c->chat[c->chat_count++];
	size_t L = strlen(line);
	if(L > LCH_CHAT_MAX_LEN) L = LCH_CHAT_MAX_LEN;
	ce->len = (u8)L;
	memcpy(ce->text, line, L);
}

static void game_stream_cb(const char *line, void *userdata){
	client_t *c = (client_t*)userdata;

	if(!line || !c) return;

	/* gameState: clocks + last move + status */
	if(strstr(line, "\"type\":\"gameState\"")){
		GameStateInfo gs;
		lch_parse_game_state_line(line, &gs);

		/* CLOCK */
		if(gs.has_wtime && gs.has_btime){
			LCH_MsgSvClock clk;
			memset(&clk, 0, sizeof(clk));
			clk.type       = LCH_SV_CLOCK;
			/* side to move from ply_count: 0 plies -> white; 1 -> black; etc */
			if(gs.ply_count & 1){
				clk.flags |= 1; /* black to move */
			}
			clk.white_secs = (u16)(gs.wtime_ms / 1000);
			clk.black_secs = (u16)(gs.btime_ms / 1000);
			lch_enqueue_to_client(c, &clk, sizeof(clk));
		}

		/* MOVE + server-side move log */
		if(gs.has_move){
			u8 from_sq, to_sq, promo;

			/* log */
			if(c->move_count < LCH_MAX_MOVES){
				if(uci_to_lch_move(gs.last_uci, &from_sq, &to_sq, &promo) == 0){
					MoveLogEntry *me = &c->moves[c->move_count++];
					me->from_sq = from_sq;
					me->to_sq   = to_sq;
					me->promo   = promo;
				}
			}

			/* OPP_MOVE / own move filtering via last_sent_uci */
			if(!(c->last_sent_valid && strcmp(gs.last_uci, c->last_sent_uci) == 0)){
				if(uci_to_lch_move(gs.last_uci, &from_sq, &to_sq, &promo) == 0){
					LCH_MsgSvOppMove mv;
					mv.type   = LCH_SV_OPP_MOVE;
					mv.from_sq= from_sq;
					mv.to_sq  = to_sq;
					mv.promo  = promo;
					lch_enqueue_to_client(c, &mv, sizeof(mv));
				}
			}
			/* after we see stream echo our last_sent_uci, invalidate it */
			if(c->last_sent_valid && strcmp(gs.last_uci, c->last_sent_uci) == 0){
				c->last_sent_valid = 0;
			}
		}

		/* GAME_END */
		if(gs.has_status && strcmp(gs.status, "started") != 0){
			LCH_MsgSvGameEnd ge;
			memset(&ge, 0, sizeof(ge));
			ge.type   = LCH_SV_GAME_END;
			ge.result = lch_map_status_to_result(gs.status, c->my_side);
			ge.reason = lch_map_status_to_reason(gs.status);
			lch_enqueue_to_client(c, &ge, sizeof(ge));
			c->stream_quit = 1;
		}

		return;
	}

	/* chatLine from Lichess */
	if(strstr(line, "\"type\":\"chatLine\"")){
		char user[32];
		char msg[64];
		char combined[80];
		LCH_MsgSvChat ch;
		size_t L;

		if(parse_string_after(line, "\"username\":\"", user, sizeof(user)) != 0){
			strcpy(user, "?");
		}
		if(parse_string_after(line, "\"text\":\"", msg, sizeof(msg)) != 0){
			msg[0] = '\0';
		}

		snprintf(combined, sizeof(combined), "%s: %s", user, msg);
		append_chat_line(c, combined);

		memset(&ch, 0, sizeof(ch));
		ch.type  = LCH_SV_CHAT;
		ch.flags = 0;
		L = strlen(combined);
		if(L > LCH_CHAT_MAX_LEN) L = LCH_CHAT_MAX_LEN;
		ch.len = (u8)L;
		memcpy(ch.text, combined, L);

		lch_enqueue_to_client(c, &ch, sizeof(ch));
		return;
	}

	/* we ignore other event types for now (gameFull, opponentGone, etc.) */
}

static void *stream_thread_main(void *arg){
	client_t *c = (client_t*)arg;
	const char *token = get_token_for_client(c);

	if(!token || !c->game_id[0]){
		c->stream_running = 0;
		return NULL;
	}

	(void)lichess_stream_game(token, c->game_id, game_stream_cb, c);

	c->stream_running = 0;
	return NULL;
}

/* --------------------------------------------------------------------- */
/* error helper                                                          */
/* --------------------------------------------------------------------- */

static void send_error(client_t *c, u8 code, u8 a0, u8 a1){
	LCH_MsgSvError e;
	e.type = LCH_SV_ERROR;
	e.code = code;
	e.arg0 = a0;
	e.arg1 = a1;
	lch_enqueue_to_client(c, &e, sizeof(e));
}

/* --------------------------------------------------------------------- */
/* LCH handlers                                                          */
/* --------------------------------------------------------------------- */

static void handle_hello(client_t *c, const LCH_MsgClHello *m){
	(void)m;
	LCH_MsgSvHello h;
	h.type        = LCH_SV_HELLO;
	h.proto_ver   = 1;
	h.capabilities= 0;
	h.reserved    = 0;
	lch_enqueue_to_client(c, &h, sizeof(h));
}

static void handle_new_game(client_t *c, const LCH_MsgClNewGame *m){
	const char *token;
	char game_id[16];
	unsigned base_seconds;
	int rc;

	token = get_token_for_client(c);
	if(!token){
		send_error(c, LCH_ERR_NO_TOKEN, 0, 0);
		return;
	}
	if(c->in_game){
		send_error(c, LCH_ERR_ALREADY_GAME, 0, 0);
		return;
	}

	/* LCH_MsgClNewGame.minutes is in minutes, Lichess wants seconds */
	base_seconds = (unsigned)m->minutes * 60u;

	rc = lichess_start_random_game(token,
		(m->flags & LCH_FLAG_RATED) ? 1 : 0,
		base_seconds,
		m->increment,
		game_id,
		sizeof(game_id));
	if(rc != 0){
		send_error(c, LCH_ERR_LICHESS_HTTP, (u8)(-rc), 0);
		return;
	}

	memset(c->game_id, 0, sizeof(c->game_id));
	strncpy(c->game_id, game_id, sizeof(c->game_id) - 1);
	c->in_game         = 1;
	c->my_side         = -1;	/* unknown until we parse gameFull, TODO */
	c->last_sent_valid = 0;
	c->move_count      = 0;
	c->chat_count      = 0;

	LCH_MsgSvGameStart s;
	memset(&s, 0, sizeof(s));
	s.type      = LCH_SV_GAME_START;
	s.flags     = m->flags;
	s.minutes   = m->minutes;
	s.increment = m->increment;
	s.my_side   = (u8)0xff;

	s.game_id_len = (u8)strlen(game_id);
	if(s.game_id_len > 8) s.game_id_len = 8;
	memcpy(s.game_id, game_id, s.game_id_len);

	lch_enqueue_to_client(c, &s, sizeof(s));

	/* start streaming thread */
	c->stream_quit    = 0;
	c->stream_running = 1;
	pthread_create(&c->stream_tid, NULL, stream_thread_main, c);
}

static void handle_move(client_t *c, const LCH_MsgClMove *m){
	const char *token;
	char uci[6];
	int rc;

	if(!c->in_game || !c->game_id[0]){
		send_error(c, LCH_ERR_NO_GAME, 0, 0);
		return;
	}
	token = get_token_for_client(c);
	if(!token){
		send_error(c, LCH_ERR_NO_TOKEN, 0, 0);
		return;
	}

	lch_move_to_uci(m->from_sq, m->to_sq, m->promo, uci);
	rc = lichess_send_move(token, c->game_id, uci, 0);
	if(rc != 0){
		send_error(c, LCH_ERR_LICHESS_HTTP, (u8)(-rc), 0);
		return;
	}

	strncpy(c->last_sent_uci, uci, sizeof(c->last_sent_uci)-1);
	c->last_sent_uci[5] = '\0';
	c->last_sent_valid  = 1;

	/* also append to move log immediately */
	if(c->move_count < LCH_MAX_MOVES){
		MoveLogEntry *me = &c->moves[c->move_count++];
		me->from_sq = m->from_sq;
		me->to_sq   = m->to_sq;
		me->promo   = m->promo;
	}
}

static void handle_resign(client_t *c, const LCH_MsgClResign *m){
	const char *token;
	int rc;
	(void)m;

	if(!c->in_game || !c->game_id[0]){
		send_error(c, LCH_ERR_NO_GAME, 0, 0);
		return;
	}
	token = get_token_for_client(c);
	if(!token){
		send_error(c, LCH_ERR_NO_TOKEN, 0, 0);
		return;
	}
	rc = lichess_resign_game(token, c->game_id);
	if(rc != 0){
		send_error(c, LCH_ERR_LICHESS_HTTP, (u8)(-rc), 0);
		return;
	}
}

static void handle_abort(client_t *c, const LCH_MsgClAbort *m){
	const char *token;
	int rc;
	(void)m;

	if(!c->in_game || !c->game_id[0]){
		send_error(c, LCH_ERR_NO_GAME, 0, 0);
		return;
	}
	token = get_token_for_client(c);
	if(!token){
		send_error(c, LCH_ERR_NO_TOKEN, 0, 0);
		return;
	}
	rc = lichess_abort_game(token, c->game_id);
	if(rc != 0){
		send_error(c, LCH_ERR_LICHESS_HTTP, (u8)(-rc), 0);
		return;
	}
}

static void handle_ping(client_t *c, const LCH_MsgClPing *m){
	LCH_MsgSvPong p;
	p.type   = LCH_SV_PONG;
	p.token  = m->token;
	p.reserved[0] = 0;
	p.reserved[1] = 0;
	lch_enqueue_to_client(c, &p, sizeof(p));
}

static void handle_chat(client_t *c, const LCH_MsgClChat *m){
	const char *token;
	char line[80];
	LCH_MsgSvChat ch;
	int rc;
	size_t L;

	if(!c->in_game || !c->game_id[0]){
		send_error(c, LCH_ERR_NO_GAME, 0, 0);
		return;
	}
	token = get_token_for_client(c);
	if(!token){
		send_error(c, LCH_ERR_NO_TOKEN, 0, 0);
		return;
	}

	/* build "You: text" for local log */
	L = (m->len > LCH_CHAT_MAX_LEN) ? LCH_CHAT_MAX_LEN : m->len;
	snprintf(line, sizeof(line), "You: %.*s", (int)L, m->text);
	append_chat_line(c, line);

	/* immediately echo back to client as chat (so they see it without waiting
	   for Lichess stream echo) */
	memset(&ch, 0, sizeof(ch));
	ch.type  = LCH_SV_CHAT;
	ch.flags = 1;	/* bit0 = local player */
	L = strlen(line);
	if(L > LCH_CHAT_MAX_LEN) L = LCH_CHAT_MAX_LEN;
	ch.len = (u8)L;
	memcpy(ch.text, line, L);
	lch_enqueue_to_client(c, &ch, sizeof(ch));

	/* forward to Lichess */
	rc = lichess_send_chat(token, c->game_id, m->text);
	if(rc != 0){
		send_error(c, LCH_ERR_LICHESS_HTTP, (u8)(-rc), 0);
	}
}

/* --------------------------------------------------------------------- */
/* history window handlers                                               */
/* --------------------------------------------------------------------- */

static void handle_req_moves(client_t *c, const LCH_MsgClReqMoves *m){
	u16 start = (u16)((m->start_hi << 8) | m->start_lo);
	u16 total = c->move_count;
	u16 i;
	u8  remaining;
	u8  want = m->count;

	if(!c->in_game || total == 0){
		return;
	}

	if(start >= total){
		start = (total > 1) ? (total - 1) : 0;
	}

	if(want == 0 || want > 32)	/* clamp */
		want = 32;

	remaining = (u8)((total - start) < want ? (total - start) : want);

	for(i = 0; i < remaining; i++){
		u16 idx = (u16)(start + i);
		MoveLogEntry *me = &c->moves[idx];
		LCH_MsgSvOppMove mv;
		mv.type   = LCH_SV_OPP_MOVE;
		mv.from_sq= me->from_sq;
		mv.to_sq  = me->to_sq;
		mv.promo  = me->promo;
		lch_enqueue_to_client(c, &mv, sizeof(mv));
	}

	/* Optional: send INFO with total move count */
	{
		LCH_MsgSvInfo info;
		info.type      = LCH_SV_INFO;
		info.info_code = LCH_INFO_MOVES_TOTAL;
		info.value0    = (u8)(total >> 8);
		info.value1    = (u8)(total & 0xff);
		lch_enqueue_to_client(c, &info, sizeof(info));
	}
}

static void handle_req_chat(client_t *c, const LCH_MsgClReqChat *m){
	u16 start = (u16)((m->start_hi << 8) | m->start_lo);
	u16 total = c->chat_count;
	u16 i;
	u8  remaining;
	u8  want = m->count;

	if(!c->in_game || total == 0){
		return;
	}

	if(start >= total){
		start = (total > 1) ? (total - 1) : 0;
	}

	if(want == 0 || want > 20)
		want = 20;

	remaining = (u8)((total - start) < want ? (total - start) : want);

	for(i = 0; i < remaining; i++){
		u16 idx = (u16)(start + i);
		ChatLogEntry *ce = &c->chat[idx];
		LCH_MsgSvChat ch;
		u8 L = ce->len;

		if(L > LCH_CHAT_MAX_LEN) L = LCH_CHAT_MAX_LEN;

		memset(&ch, 0, sizeof(ch));
		ch.type  = LCH_SV_CHAT;
		ch.flags = 0;		/* history; you can add a flag bit if you like */
		ch.len   = L;
		memcpy(ch.text, ce->text, L);

		lch_enqueue_to_client(c, &ch, sizeof(ch));
	}

	/* Optional: send INFO with total chat count */
	{
		LCH_MsgSvInfo info;
		info.type      = LCH_SV_INFO;
		info.info_code = LCH_INFO_CHAT_TOTAL;
		info.value0    = (u8)(total >> 8);
		info.value1    = (u8)(total & 0xff);
		lch_enqueue_to_client(c, &info, sizeof(info));
	}
}

/* --------------------------------------------------------------------- */
/* dispatch LCH payload from tunnel DATA                                 */
/* --------------------------------------------------------------------- */

static void handle_client_frame(client_t *c, const u8 *buf, u8 len){
	u8 type;

	if(len == 0) return;
	type = buf[0];

	switch(type){
	case LCH_CL_HELLO:
		if(len >= sizeof(LCH_MsgClHello)){
			handle_hello(c, (const LCH_MsgClHello*)buf);
		}
		break;
	case LCH_CL_NEW_GAME:
		if(len >= sizeof(LCH_MsgClNewGame)){
			handle_new_game(c, (const LCH_MsgClNewGame*)buf);
		}
		break;
	case LCH_CL_MOVE:
		if(len >= sizeof(LCH_MsgClMove)){
			handle_move(c, (const LCH_MsgClMove*)buf);
		}
		break;
	case LCH_CL_RESIGN:
		if(len >= sizeof(LCH_MsgClResign)){
			handle_resign(c, (const LCH_MsgClResign*)buf);
		}
		break;
	case LCH_CL_ABORT:
		if(len >= sizeof(LCH_MsgClAbort)){
			handle_abort(c, (const LCH_MsgClAbort*)buf);
		}
		break;
	case LCH_CL_PING:
		if(len >= sizeof(LCH_MsgClPing)){
			handle_ping(c, (const LCH_MsgClPing*)buf);
		}
		break;
	case LCH_CL_CHAT:
		if(len >= sizeof(LCH_MsgClChat)){
			handle_chat(c, (const LCH_MsgClChat*)buf);
		}
		break;
	case LCH_CL_REQ_MOVES:
		if(len >= sizeof(LCH_MsgClReqMoves)){
			handle_req_moves(c, (const LCH_MsgClReqMoves*)buf);
		}
		break;
	case LCH_CL_REQ_CHAT:
		if(len >= sizeof(LCH_MsgClReqChat)){
			handle_req_chat(c, (const LCH_MsgClReqChat*)buf);
		}
		break;
	default:
		send_error(c, LCH_ERR_GENERIC, type, 0);
		break;
	}
}

/* --------------------------------------------------------------------- */
/* LOGIN meta (tunnel)                                                   */
/* --------------------------------------------------------------------- */

static void handle_login_meta(client_t *c, const u8 *data, u16 len){
	const TunnelLoginMeta *m;

	if(len < sizeof(TunnelLoginMeta)) return;
	m = (const TunnelLoginMeta*)data;
	c->user_id = m->user_id;

	/* pre-load prefs so we have token ready for first NEW_GAME */
	load_prefs_for_client(c);

	/* send HELLO once we know who this is */
	LCH_MsgSvHello h;
	h.type        = LCH_SV_HELLO;
	h.proto_ver   = 1;
	h.capabilities= 0;
	h.reserved    = 0;
	lch_enqueue_to_client(c, &h, sizeof(h));
}

/* --------------------------------------------------------------------- */
/* socket setup                                                          */
/* --------------------------------------------------------------------- */

static int setup_listen_socket(const char *path){
	int fd;
	struct sockaddr_un addr;

	unlink(path);

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if(fd < 0) return -1;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path)-1);

	if(bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
		close(fd);
		return -2;
	}
	if(listen(fd, 16) < 0){
		close(fd);
		return -3;
	}
	set_nonblock(fd);
	return fd;
}

/* --------------------------------------------------------------------- */
/* main                                                                  */
/* --------------------------------------------------------------------- */

int main(int argc, char **argv){
	int i;
	int rc;
	(void)argc;
	(void)argv;

	openlog("uzenet-lichess", LOG_PID, LOG_DAEMON);

	signal(SIGINT, on_sigint);
	signal(SIGTERM, on_sigint);

	if(lichess_init() != 0){
		syslog(LOG_ERR, "lichess_init (curl_global_init) failed");
		return 1;
	}

	g_listen_fd = setup_listen_socket(LICHESS_SOCK_PATH);
	if(g_listen_fd < 0){
		syslog(LOG_ERR, "failed to setup listen socket %s", LICHESS_SOCK_PATH);
		return 1;
	}

	for(i=0;i<LCH_MAX_CLIENTS;i++){
		g_clients[i].state = CLST_UNUSED;
	}

	syslog(LOG_INFO, "uzenet-lichess listening on %s", LICHESS_SOCK_PATH);

	while(g_running){
		struct pollfd pfds[1 + LCH_MAX_CLIENTS];
		int nfds = 0;

		/* listen fd */
		pfds[nfds].fd = g_listen_fd;
		pfds[nfds].events = POLLIN;
		pfds[nfds].revents = 0;
		nfds++;

		/* client fds */
		for(i=0;i<LCH_MAX_CLIENTS;i++){
			if(g_clients[i].state == CLST_ACTIVE){
				pfds[nfds].fd = g_clients[i].fd;
				pfds[nfds].events = POLLIN | POLLOUT;
				pfds[nfds].revents = 0;
				nfds++;
			}
		}

		rc = poll(pfds, nfds, 1000);
		if(rc < 0){
			if(errno == EINTR) continue;
			break;
		}

		/* accept new */
		if(pfds[0].revents & POLLIN){
			int cfd = accept(g_listen_fd, NULL, NULL);
			if(cfd >= 0){
				client_t *c;
				set_nonblock(cfd);
				c = alloc_client(cfd);
				if(!c){
					close(cfd);
				}
			}
		}

		/* handle clients */
		{
			int idx = 1;
			for(i=0;i<LCH_MAX_CLIENTS;i++){
				client_t *c = &g_clients[i];
				if(c->state != CLST_ACTIVE) continue;

				short re = pfds[idx].revents;
				int fd   = pfds[idx].fd;
				idx++;

				if(re & (POLLHUP | POLLERR | POLLNVAL)){
					free_client(c);
					continue;
				}

				/* readable: tunnel frame */
				if(re & POLLIN){
					TunnelFrame fr;
					int r2 = ReadTunnelFramed(fd, &fr);
					if(r2 == 0){
						/* EOF */
						free_client(c);
						continue;
					}else if(r2 < 0){
						/* error */
						free_client(c);
						continue;
					}

					if(fr.type == TUNNEL_TYPE_LOGIN){
						handle_login_meta(c, fr.data, fr.length);
					}else if(fr.type == TUNNEL_TYPE_DATA){
						if(fr.length > 0){
							handle_client_frame(c, fr.data, (u8)fr.length);
						}
					}else{
						/* unknown type, ignore for now */
					}
				}

				/* writable: flush queue */
				if(re & POLLOUT){
					u8 buf[LCH_OUTQ_SLOT_BYTES];
					u8 len;
					if(lch_dequeue_from_client(c, buf, &len) == 0){
						if(send_lch_msg(c, buf, len) < 0){
							free_client(c);
							continue;
						}
					}
				}
			}
		}
	}

	close(g_listen_fd);
	unlink(LICHESS_SOCK_PATH);
	lichess_shutdown();
	syslog(LOG_INFO, "uzenet-lichess exiting");
	closelog();
	return 0;
}
