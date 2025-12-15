/*
 * uzenet-zipstream-server.c
 *
 * Uzenet tunnel service:
 *  - Listens on Unix domain socket /run/uzenet/zipstream.sock
 *  - Each tunnel carries a simple text protocol:
 *        "Unzip http://host.com/file.zip\n"
 *  - Server discovers uncompressed size, sends 32-bit BE length,
 *    then streams raw uncompressed bytes.
 *
 * Access is only via Uzenet tunnel (uzenet-room), not a public TCP port.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <time.h>

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <syslog.h>
#include <errno.h>

#include <curl/curl.h>
#include "miniz.h"
#include "uzenet-tunnel.h"	// TunnelFrame, TUNNEL_TYPE_*, ReadTunnelFramed, WriteTunnelFramed

typedef int sock_t;
#define close_socket(s)	close(s)
#define WRITE(s,b,n)	write((s),(b),(n))

#define BACKLOG				32
#define CMD_BUF_LEN			256
#define MAX_EOCD_SEARCH		0x10000	// last 64KB
#define ZIPSTREAM_SOCKET_PATH	"/run/uzenet/zipstream.sock"

struct mem_range {
	unsigned char *data;
	size_t		 size;
};

static size_t curl_mem_cb(void *ptr, size_t sz, size_t nm, void *ud){
	size_t len = sz * nm;
	struct mem_range *m = (struct mem_range*)ud;
	unsigned char *p = realloc(m->data, m->size + len);
	if(!p) return 0;
	m->data = p;
	memcpy(p + m->size, ptr, len);
	m->size += len;
	return len;
}

// fetch byte range [range] from URL into heap buffer
static int fetch_range(const char *url, const char *range, struct mem_range *out){
	CURL *c = curl_easy_init();
	if(!c) return 0;
	out->data = NULL;
	out->size = 0;
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_RANGE, range);	// e.g. "bytes=-65536"
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_mem_cb);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, out);
	CURLcode rc = curl_easy_perform(c);
	curl_easy_cleanup(c);
	return (rc == CURLE_OK);
}

// Read up to (and including) a '\n' into out[], NUL-terminate.
// Returns number of bytes (including the '\n'), or –1 on error/EOF.
static int read_cmd_line(sock_t s, char *out, size_t maxlen){
	size_t pos = 0;
	while(pos + 1 < maxlen){
		char c;
		ssize_t r = recv(s, &c, 1, 0);
		if(r <= 0) return -1;
		out[pos++] = c;
		if(c == '\n') break;
	}
	out[pos] = '\0';
	return (int)pos;
}

static uint16_t le16(const unsigned char *p){
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const unsigned char *p){
	return (uint32_t)p[0]
		| ((uint32_t)p[1] << 8)
		| ((uint32_t)p[2] << 16)
		| ((uint32_t)p[3] << 24);
}

// discover uncompressed size by reading Central Directory
// returns 1 on success, fills *size_out, else 0.
static int get_uncompressed_size(const char *url, uint32_t *size_out){
	struct mem_range tail = {0};
	char range_hdr[32];

	// fetch last MAX_EOCD_SEARCH bytes
	snprintf(range_hdr, sizeof(range_hdr), "bytes=-%d", MAX_EOCD_SEARCH);
	if(!fetch_range(url, range_hdr, &tail)) return 0;
	if(tail.size < 22){
		free(tail.data);
		return 0;
	}

	// find End-of-Central-Directory (EOCD) signature 0x06054b50
	ssize_t i;
	for(i = (ssize_t)tail.size - 22; i >= 0; i--){
		if(le32(tail.data + i) == 0x06054b50) break;
	}
	if(i < 0){
		free(tail.data);
		return 0;
	}

	// read central directory size & offset
	uint32_t cd_size   = le32(tail.data + i + 12);
	uint32_t cd_offset = le32(tail.data + i + 16);
	free(tail.data);

	// fetch exactly the central directory
	struct mem_range cd = {0};
	snprintf(range_hdr, sizeof(range_hdr),
		"bytes=%u-%u",
		cd_offset, cd_offset + cd_size - 1);
	if(!fetch_range(url, range_hdr, &cd)) return 0;
	if(cd.size < 46){
		free(cd.data);
		return 0;
	}

	// locate first Central Directory File Header (signature 0x02014b50)
	ssize_t j;
	for(j = 0; j + 4 < (ssize_t)cd.size; j++){
		if(le32(cd.data + j) == 0x02014b50) break;
	}
	if(j + 30 >= (ssize_t)cd.size){
		free(cd.data);
		return 0;
	}

	// extract uncompressed size at offset j+24
	uint32_t uncomp = le32(cd.data + j + 24);
	free(cd.data);

	*size_out = uncomp;
	return 1;
}

static void urldecode(char *dst, const char *src){
	char hex[3] = {0};
	while(*src){
		if(*src=='%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])){
			hex[0]=src[1]; hex[1]=src[2];
			*dst = (char)strtol(hex, NULL, 16);
			src += 3;
		}else if(*src=='+'){
			*dst=' '; src++;
		}else{
			*dst = *src++;
		}
		dst++;
	}
	*dst = '\0';
}

typedef struct{
	sock_t			  client;
	tinfl_decompressor  decomp;
	unsigned char	   in_buf[4096];
	size_t			  in_size;
	unsigned char	   header[64];
	size_t			  hdr_received;
	size_t			  hdr_needed;
	int				 state;	// 0 = header, 1 = decompress
} ctx_t;

static size_t extract_cb(void *opaque, mz_uint64 offset, const void *buf, size_t len){
	sock_t c = *(sock_t*)opaque;
	(void)offset;
	return (size_t)WRITE(c, buf, len);
}

// curl write callback: feed ZIP bytes into header parse + tinfl
static size_t curl_cb(void *ptr, size_t sz, size_t nm, void *ud){
	size_t len = sz * nm;
	ctx_t *ctx = (ctx_t*)ud;
	size_t off = 0;

	// parse local file header on the fly
	if(ctx->state == 0){
		// first 30 bytes fixed
		if(ctx->hdr_received < 30){
			size_t want = 30 - ctx->hdr_received;
			size_t take = len < want ? len : want;
			memcpy(ctx->header + ctx->hdr_received, (unsigned char*)ptr + off, take);
			ctx->hdr_received += take;
			off += take;
			if(ctx->hdr_received == 30){
				uint16_t nlen = le16(ctx->header + 26);
				uint16_t elen = le16(ctx->header + 28);
				ctx->hdr_needed = 30 + nlen + elen;
			}
		}
		// filename+extra
		if(off < len && ctx->hdr_received < ctx->hdr_needed){
			size_t want = ctx->hdr_needed - ctx->hdr_received;
			size_t take = (len - off) < want ? (len - off) : want;
			memcpy(ctx->header + ctx->hdr_received, (unsigned char*)ptr + off, take);
			ctx->hdr_received += take;
			off += take;
		}
		if(ctx->hdr_received >= ctx->hdr_needed){
			tinfl_init(&ctx->decomp);
			ctx->state = 1;
			ctx->in_size = 0;
		}
	}

	// stream-decompress remainder
	if(ctx->state == 1 && off < len){
		size_t want = len - off;
		size_t room = sizeof(ctx->in_buf) - ctx->in_size;
		size_t take = want < room ? want : room;
		memcpy(ctx->in_buf + ctx->in_size, (unsigned char*)ptr + off, take);
		ctx->in_size += take;
		off += take;

		// decompress as far as possible
		while(ctx->in_size){
			size_t in_bytes = ctx->in_size;
			size_t out_bytes = 0;
			tinfl_status st = tinfl_decompress(
				&ctx->decomp,
				ctx->in_buf, &in_bytes,
				NULL, NULL, &out_bytes,
				TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUFFER
			);
			if(out_bytes)
				extract_cb(&ctx->client, 0, ctx->decomp.m_output_buffer, out_bytes);
			// slide out consumed
			memmove(ctx->in_buf, ctx->in_buf + in_bytes, ctx->in_size - in_bytes);
			ctx->in_size -= in_bytes;
			if(st == TINFL_STATUS_DONE || st == TINFL_STATUS_FAILED)
				break;
			if(st == TINFL_STATUS_NEEDS_MORE_INPUT)
				break;
		}
	}
	return len;
}

// Original inner handler: sees a plain stream with the "Unzip ..." protocol.
static void handle_client_inner(sock_t client){
	// wait 4 seconds for "Unzip "
	fd_set rfds;
	struct timeval tv = {4, 0};
	FD_ZERO(&rfds);
	FD_SET(client, &rfds);
	if(select(client + 1, &rfds, NULL, NULL, &tv) <= 0){
		syslog(LOG_WARNING, "ZipStream: timeout waiting for command");
		close_socket(client);
		return;
	}

	// read command line
	char line[CMD_BUF_LEN];
	int n = read_cmd_line(client, line, sizeof(line));
	if(n <= 0){
		syslog(LOG_WARNING, "ZipStream: recv error");
		close_socket(client);
		return;
	}

	// check verb
	if(strncmp(line, "Unzip ", 6) != 0){
		syslog(LOG_WARNING, "ZipStream: bad cmd: %.40s", line);
		close_socket(client);
		return;
	}

	// decode URL
	char url_enc[1024], url[1024];
	strncpy(url_enc, line + 6, sizeof(url_enc) - 1);
	url_enc[sizeof(url_enc) - 1] = 0;
	// strip newline / CR / trailing spaces
	for(int i = (int)strlen(url_enc) - 1; i >= 0; i--){
		if(url_enc[i] == '\r' || url_enc[i] == '\n' || url_enc[i] == ' ')
			url_enc[i] = 0;
		else
			break;
	}
	urldecode(url, url_enc);

	// discover uncompressed size
	uint32_t uncomp;
	if(!get_uncompressed_size(url, &uncomp)){
		syslog(LOG_ERR, "ZipStream: size discovery failed for URL: %s", url);
		close_socket(client);
		return;
	}

	// send 32-bit network order length
	uint32_t netlen = htonl(uncomp);
	WRITE(client, &netlen, sizeof(netlen));

	// stream-decompress the ZIP entry
	ctx_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.client       = client;
	ctx.hdr_received = 0;
	ctx.hdr_needed   = 30;
	ctx.state        = 0;
	ctx.in_size      = 0;

	CURL *c = curl_easy_init();
	if(c){
		curl_easy_setopt(c, CURLOPT_URL,            url);
		curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(c, CURLOPT_WRITEFUNCTION,  curl_cb);
		curl_easy_setopt(c, CURLOPT_WRITEDATA,      &ctx);
		curl_easy_perform(c);
		curl_easy_cleanup(c);
	}

	close_socket(client);
}

/* ---------- Tunnel bridge ---------- */

typedef struct{
	uint16_t user_id;
	uint16_t reserved;
} TunnelLoginMeta;

static int write_full_stream(int fd, const void *buf, size_t len){
	const uint8_t *p = (const uint8_t*)buf;
	while(len){
		ssize_t w = write(fd, p, len);
		if(w < 0){
			if(errno == EINTR) continue;
			return -1;
		}
		if(w == 0) return -1;
		p   += w;
		len -= (size_t)w;
	}
	return 0;
}

static void *zipstream_worker_thread(void *arg){
	sock_t c = (sock_t)(uintptr_t)arg;
	handle_client_inner(c);
	return NULL;
}

typedef struct{
	int tunnel_fd;
} TunnelClientArgs;

static void *tunnel_client_thread(void *arg){
	TunnelClientArgs *ta = (TunnelClientArgs*)arg;
	int tunnel_fd = ta->tunnel_fd;
	free(ta);

	TunnelFrame fr;
	int have_first_data = 0;

	// First frame: LOGIN meta or DATA
	int r = ReadTunnelFramed(tunnel_fd, &fr);
	if(r <= 0){
		close(tunnel_fd);
		return NULL;
	}

	if(fr.type == TUNNEL_TYPE_LOGIN && fr.length >= sizeof(TunnelLoginMeta)){
		const TunnelLoginMeta *meta = (const TunnelLoginMeta*)fr.data;
		syslog(LOG_INFO, "ZipStream: LOGIN user_id=%u", (unsigned)meta->user_id);
	}else if(fr.type == TUNNEL_TYPE_DATA && fr.length > 0){
		have_first_data = 1;
	}else{
		// ignore other types, fall through
	}

	// Create local socketpair: [0] bridge <-> [1] worker
	int sp[2];
	if(socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0){
		syslog(LOG_ERR, "ZipStream: socketpair failed: %s", strerror(errno));
		close(tunnel_fd);
		return NULL;
	}

	int bridge_fd = sp[0];
	sock_t worker_sock = (sock_t)sp[1];

	// Spawn worker that runs the original Unzip protocol
	pthread_t worker_tid;
	if(pthread_create(&worker_tid, NULL, zipstream_worker_thread,
		(void*)(uintptr_t)worker_sock) != 0){
		syslog(LOG_ERR, "ZipStream: pthread_create worker failed: %s", strerror(errno));
		close(bridge_fd);
		close(worker_sock);
		close(tunnel_fd);
		return NULL;
	}
	pthread_detach(worker_tid);

	// If first frame was DATA, inject it as initial bytes to worker
	if(have_first_data && fr.length > 0){
		if(write_full_stream(bridge_fd, fr.data, fr.length) < 0){
			close(bridge_fd);
			close(tunnel_fd);
			return NULL;
		}
	}

	// Bridge loop: tunnel_fd <-> bridge_fd
	for(;;){
		fd_set rfds;
		int maxfd = (tunnel_fd > bridge_fd ? tunnel_fd : bridge_fd) + 1;

		FD_ZERO(&rfds);
		FD_SET(tunnel_fd, &rfds);
		FD_SET(bridge_fd, &rfds);

		int n = select(maxfd, &rfds, NULL, NULL, NULL);
		if(n < 0){
			if(errno == EINTR) continue;
			break;
		}

		// Tunnel -> worker
		if(FD_ISSET(tunnel_fd, &rfds)){
			TunnelFrame in_fr;
			int rt = ReadTunnelFramed(tunnel_fd, &in_fr);
			if(rt <= 0){
				// remote closed or error
				break;
			}
			if(in_fr.type == TUNNEL_TYPE_DATA && in_fr.length > 0){
				if(write_full_stream(bridge_fd, in_fr.data, in_fr.length) < 0){
					break;
				}
			}
			// ignore other types for now
		}

		// Worker -> tunnel
		if(FD_ISSET(bridge_fd, &rfds)){
			uint8_t buf[1024];
			ssize_t rd = read(bridge_fd, buf, sizeof(buf));
			if(rd <= 0){
				// worker closed
				break;
			}
			size_t off = 0;
			while(off < (size_t)rd){
				size_t chunk = (size_t)rd - off;
				if(chunk > TUNNEL_MAX_PAYLOAD)
					chunk = TUNNEL_MAX_PAYLOAD;

				TunnelFrame out;
				out.type   = TUNNEL_TYPE_DATA;
				out.flags  = 0;
				out.length = (uint16_t)chunk;
				memcpy(out.data, buf + off, chunk);

				if(WriteTunnelFramed(tunnel_fd, &out) < 0){
					// remote closed
					break;
				}
				off += chunk;
			}
		}
	}

	close(bridge_fd);
	close(tunnel_fd);
	return NULL;
}

/* ---------- main: Unix socket listener only ---------- */

int main(void){
	curl_global_init(CURL_GLOBAL_DEFAULT);
	openlog("ZipStream", LOG_PID | LOG_NDELAY, LOG_DAEMON);

	int srv = socket(AF_UNIX, SOCK_STREAM, 0);
	if(srv < 0){
		syslog(LOG_ERR, "ZipStream: socket failed: %s", strerror(errno));
		return 1;
	}

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, ZIPSTREAM_SOCKET_PATH, sizeof(addr.sun_path) - 1);
	unlink(ZIPSTREAM_SOCKET_PATH);

	if(bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0){
		syslog(LOG_ERR, "ZipStream: bind failed on %s: %s",
			ZIPSTREAM_SOCKET_PATH, strerror(errno));
		close(srv);
		return 1;
	}
	if(listen(srv, BACKLOG) < 0){
		syslog(LOG_ERR, "ZipStream: listen failed: %s", strerror(errno));
		close(srv);
		return 1;
	}

	syslog(LOG_INFO, "ZipStream: listening on %s", ZIPSTREAM_SOCKET_PATH);

	for(;;){
		int tfd = accept(srv, NULL, NULL);
		if(tfd < 0){
			if(errno == EINTR) continue;
			syslog(LOG_ERR, "ZipStream: accept failed: %s", strerror(errno));
			continue;
		}
		TunnelClientArgs *ta = malloc(sizeof(*ta));
		if(!ta){
			close(tfd);
			continue;
		}
		ta->tunnel_fd = tfd;

		pthread_t tid;
		if(pthread_create(&tid, NULL, tunnel_client_thread, ta) != 0){
			syslog(LOG_ERR, "ZipStream: pthread_create client failed: %s", strerror(errno));
			close(tfd);
			free(ta);
			continue;
		}
		pthread_detach(tid);
	}

	// not reached
	close(srv);
	closelog();
	curl_global_cleanup();
	return 0;
}
