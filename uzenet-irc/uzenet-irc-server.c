/*
 * uzenet-irc-server.c  (phase5: highlights & formatting)
 * --------------------------------------------------------
 * Full-featured Uzenet IRC proxy:
 *   • TLS + SASL PLAIN auth
 *   • Nick collision resolution (433 → append '_')
 *   • JOIN / PART / PRIVMSG proxy
 *   • Multi-channel mapping (up to 8 channels)
 *   • WHO caching → PKT_USERLIST
 *   • Scrollback replay (200 lines)
 *   • Strip IRC color/format codes
 *   • Simple UTF-8→CP437 fallback
 *   • Highlight keywords → PKT_BEEP
 *   • 80-column word wrap
 *
 * Build with:
 *   gcc -Wall -Wextra -O2 -pthread \
 *       -levent -levent_extra -levent_openssl -lssl -lcrypto \
 *       -o uzenet-irc-server uzenet-irc-server.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/bufferevent_ssl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <arpa/inet.h>

#define DEFAULT_IRC_SERVER   "irc.libera.chat"
#define DEFAULT_IRC_PORT     6697
#define DEFAULT_LISTEN_PORT  57431
#define MAX_LINE             512
#define WRAP_COLS            80
#define SCROLLBACK_LINES     200
#define MAX_CHANNELS         8
#define MAX_USERS           256

/* Opcodes to Uzebox */
#define PKT_USERLIST 0x95
#define PKT_BEEP     0x91

/* SASL credentials */
#define SASL_USER  "yournick"
#define SASL_PASS  "yourpass"

/* Highlight keywords (include your nick) */
static const char *keywords[] = { "yournick", "urgent", "alert" };
static const int keyword_count = sizeof(keywords)/sizeof(keywords[0]);

/* Client list and scrollback */
struct Client {
	struct bufferevent *bev;
	struct Client *next;
};
static struct Client *clients = NULL;
static char *scrollback[SCROLLBACK_LINES];
static int sb_head = 0, sb_count = 0;

/* IRC connection and SASL state */
static struct event_base *base;
static SSL_CTX *ssl_ctx;
static struct bufferevent *upstream_bev;
static char cur_nick[32] = SASL_USER;
static int sasl_stage = 0;

/* Channel mapping & WHO cache */
static char *chan_names[MAX_CHANNELS];
static int chan_count = 0;
static char *who_cache[MAX_CHANNELS][MAX_USERS];
static int  who_count[MAX_CHANNELS];
static int  who_stage[MAX_CHANNELS];

/* Base64 encode utility */
static char *b64_encode(const char *in){
	BIO *b64 = BIO_new(BIO_f_base64());
	BIO *mem = BIO_new(BIO_s_mem());
	b64 = BIO_push(b64, mem);
	BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
	BIO_write(b64, in, strlen(in));
	BIO_flush(b64);
	BUF_MEM *bptr;
	BIO_get_mem_ptr(b64, &bptr);
	char *out = malloc(bptr->length+1);
	memcpy(out, bptr->data, bptr->length);
	out[bptr->length] = '\0';
	BIO_free_all(b64);
	return out;
}

/* Scrollback add & replay */
static void sb_add(const char *line){
	if(scrollback[sb_head]) free(scrollback[sb_head]);
	scrollback[sb_head] = strdup(line);
	sb_head = (sb_head+1) % SCROLLBACK_LINES;
	if(sb_count < SCROLLBACK_LINES) sb_count++;
}

/* Strip IRC color/format codes + UTF-8 fallback */
static void strip_codes(const char *in, char *out){
	while(*in){
		if(*in == '\x03'){
			in++;
			while(isdigit((unsigned char)*in)) in++;
			if(*in == ','){
				in++;
				while(isdigit((unsigned char)*in)) in++;
			}
		}else if(*in == '\x02' || *in == '\x1F' ||
				   *in == '\x16' || *in == '\x0F'){
			in++;
		}else{
			unsigned char c = (unsigned char)*in;
			if(c < 0x80){
				*out++ = *in++;
			}else{
				in++;
				*out++ = '?';
			}
		}
	}
	*out = '\0';
}

/* Send beep opcode */
static void send_beep(struct bufferevent *bev){
	uint8_t op = PKT_BEEP;
	bufferevent_write(bev, &op, 1);
}

/* Send wrapped line with highlight detection */
static void send_wrapped(struct bufferevent *bev, const char *raw){
	char clean[MAX_LINE];
	strip_codes(raw, clean);
	/* highlight? */
	for(int k = 0; k < keyword_count; k++){
		if(strcasestr(clean, keywords[k])){
			send_beep(bev);
			break;
		}
	}
	int len = strlen(clean);
	for(int i = 0; i < len; i += WRAP_COLS){
		int c = (len - i > WRAP_COLS ? WRAP_COLS : len - i);
		bufferevent_write(bev, clean + i, c);
		bufferevent_write(bev, "\r\n", 2);
	}
}

/* Broadcast to all clients */
static void broadcast(const char *line){
	sb_add(line);
	for(struct Client *c = clients; c; c = c->next){
		send_wrapped(c->bev, line);
	}
}

/* Channel management */
static int channel_id(const char *name){
	for(int i = 0; i < chan_count; i++)
		if(strcmp(chan_names[i], name) == 0)
			return i;
	if(chan_count >= MAX_CHANNELS)
		return -1;
	chan_names[chan_count] = strdup(name);
	who_stage[chan_count] = 0;
	who_count[chan_count] = 0;
	return chan_count++;
}

/* Emit USERLIST: PKT_USERLIST id,count,name... */
static void send_userlist(int cid){
	int count = who_count[cid];
	for(struct Client *c = clients; c; c = c->next){
		struct bufferevent *bev = c->bev;
		uint8_t hdr = PKT_USERLIST;
		bufferevent_write(bev, &hdr, 1);
		uint8_t id = cid;
		bufferevent_write(bev, &id, 1);
		uint8_t cnt = count;
		bufferevent_write(bev, &cnt, 1);
		for(int i = 0; i < count; i++){
			uint8_t len = strlen(who_cache[cid][i]);
			bufferevent_write(bev, &len, 1);
			bufferevent_write(bev, who_cache[cid][i], len);
		}
	}
}

/* Parse numeric reply code */
static int parse_code(const char *line){
	if(strlen(line) > 4 &&
		isdigit(line[1]) &&
		isdigit(line[2]) &&
		isdigit(line[3]))
	{
		return (line[1]-'0')*100 + (line[2]-'0')*10 + (line[3]-'0');
	}
	return 0;
}

/* Upstream read handler */
static void on_upstream_read(struct bufferevent *bev, void *ctx){
	char raw[MAX_LINE];
	while(1){
		int n = bufferevent_read(bev, raw, MAX_LINE-1);
		if(n <= 0) break;
		raw[n] = '\0';
		int code = parse_code(raw);

		/* SASL PLAIN flow */
		if(sasl_stage == 1 && code == 904){
			sasl_stage = 2;
			char tmp[64];
			snprintf(tmp, sizeof tmp, "NICK %s\r\n", cur_nick);
			bufferevent_write(bev, tmp, strlen(tmp));
			snprintf(tmp, sizeof tmp, "USER %s 0 * :uzenet\r\n", cur_nick);
			bufferevent_write(bev, tmp, strlen(tmp));

		}else if(sasl_stage == 0 && strstr(raw, "CAP * ACK :sasl")){
			char creds[128];
			snprintf(creds, sizeof creds, "%s\0%s\0%s",
					 SASL_USER, SASL_USER, SASL_PASS);
			char *b64 = b64_encode(creds);
			char buf[256];
			snprintf(buf, sizeof buf, "AUTHENTICATE %s\r\n", b64);
			bufferevent_write(bev, buf, strlen(buf));
			free(b64);
			sasl_stage = 1;

		}else if(code == 433){
			/* nick in use */
			if(strlen(cur_nick) + 2 < sizeof cur_nick){
				strcat(cur_nick, "_");
			}
			char tmp[64];
			snprintf(tmp, sizeof tmp, "NICK %s\r\n", cur_nick);
			bufferevent_write(bev, tmp, strlen(tmp));

		}else if(code == 352 || code == 315){
			/* WHO reply or end */
			if(code == 352){
				char chan[64], nick[64];
				/* skip ":server 352 nick " */
				char *p = raw;
				for(int i = 0; i < 2; i++){
					p = strchr(p+1, ' ');
					if(!p) break;
				}
				if(p && sscanf(p+1, "%63s %*s %*s %63s", chan, nick) == 2){
					int cid = channel_id(chan);
					if(cid >= 0 && who_stage[cid] && who_count[cid] < MAX_USERS){
						who_cache[cid][who_count[cid]++] = strdup(nick);
					}
				}
			}else{
				/* 315 end WHO */
				char chan[64];
				char *p = strchr(raw+1, ' ');
				p = p ? strchr(p+1, ' ') : NULL;
				if(p && sscanf(p+1, "%63s", chan) == 1){
					int cid = channel_id(chan);
					if(cid >= 0 && who_stage[cid]){
						send_userlist(cid);
						who_stage[cid] = 0;
					}
				}
			}
		}

		broadcast(raw);
	}
}

/* Upstream event handler */
static void on_upstream_event(struct bufferevent *bev, short events, void *ctx){
	if(events & BEV_EVENT_CONNECTED){
		bufferevent_write(bev, "CAP REQ :sasl\r\n", 13);
		sasl_stage = 0;
	}else if(events & (BEV_EVENT_ERROR|BEV_EVENT_EOF)){
		event_base_loopexit(base, NULL);
	}
}

/* Client read handler */
static void on_client_read(struct bufferevent *bev, void *ctx){
	char buf[MAX_LINE];
	int n = bufferevent_read(bev, buf, MAX_LINE-1);
	if(n <= 0) return;
	buf[n] = '\0';

	/* Local commands */
	if(strncmp(buf, "JOIN ", 5) == 0){
		char chan[64];
		if(sscanf(buf+5, "%63s", chan) == 1){
			channel_id(chan);
			bufferevent_write(upstream_bev, buf, n);
		}
	}
	else if(strncmp(buf, "PART ", 5) == 0){
		bufferevent_write(upstream_bev, buf, n);
	}
	else if(strncmp(buf, "WHO ", 4) == 0){
		char chan[64];
		if(sscanf(buf+4, "%63s", chan) == 1){
			int cid = channel_id(chan);
			if(cid >= 0) who_stage[cid] = 1, who_count[cid] = 0;
			bufferevent_write(upstream_bev, buf, n);
		}
	}
	else {
		/* PRIVMSG, NICK, MODE, etc. */
		bufferevent_write(upstream_bev, buf, n);
	}
}

/* Client event handler */
static void on_client_event(struct bufferevent *bev, short events, void *ctx){
	struct Client **cp = &clients;
	while(*cp){
		if((*cp)->bev == bev){
			struct Client *t = *cp;
			*cp = t->next;
			bufferevent_free(t->bev);
			free(t);
			break;
		}
		cp = &(*cp)->next;
	}
}

/* Accept new client */
static void on_accept(struct evconnlistener *listener,
					  evutil_socket_t fd,
					  struct sockaddr *addr,
					  int socklen,
					  void *ctx)
{
	struct bufferevent *bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
	struct Client *c = malloc(sizeof(*c));
	c->bev = bev;
	c->next = clients;
	clients = c;

	bufferevent_setcb(bev, on_client_read, NULL, on_client_event, NULL);
	bufferevent_enable(bev, EV_READ|EV_WRITE);

	/* Replay scrollback */
	for(int i = 0; i < sb_count; i++){
		int idx = (sb_head + i) % SCROLLBACK_LINES;
		send_wrapped(bev, scrollback[idx]);
	}
	fprintf(stderr, "Client connected, replayed %d lines\n", sb_count);
}

int main(){
	signal(SIGPIPE, SIG_IGN);
	SSL_library_init();
	SSL_load_error_strings();
	ssl_ctx = SSL_CTX_new(TLS_client_method());
	base    = event_base_new();

	/* Upstream IRC TLS connect */
	upstream_bev = bufferevent_openssl_socket_new(
		base, -1, SSL_new(ssl_ctx),
		BUFFEREVENT_SSL_CONNECTING,
		BEV_OPT_CLOSE_ON_FREE
	);
	bufferevent_setcb(upstream_bev,
					  on_upstream_read, NULL,
					  on_upstream_event, NULL);
	bufferevent_enable(upstream_bev, EV_READ|EV_WRITE);

	struct sockaddr_in sin = {
		.sin_family = AF_INET,
		.sin_port   = htons(DEFAULT_IRC_PORT),
		.sin_addr.s_addr = INADDR_ANY,
	};
	bufferevent_socket_connect(upstream_bev,
		(struct sockaddr*)&sin, sizeof(sin));

	/* Listen for Uzebox clients */
	evconnlistener_new_bind(base,
		on_accept, NULL,
		LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE,
		-1,
		(struct sockaddr*)&(struct sockaddr_in){
			.sin_family = AF_INET,
			.sin_port   = htons(DEFAULT_LISTEN_PORT),
			.sin_addr.s_addr = INADDR_ANY,
		},
		sizeof(struct sockaddr_in)
	);

	fprintf(stderr, "uzenet-irc-server listening on %d\n", DEFAULT_LISTEN_PORT);
	event_base_dispatch(base);

	SSL_CTX_free(ssl_ctx);
	event_base_free(base);
	return 0;
}
