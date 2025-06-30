/*
 * lightweight “Unzip” command server:
 *  -listens on TCP port 38030
 * -on each connection, within 4 seconds, client sends "Unzip http://host.com/file.zip", followed by \n
 * -server determines uncompressed file size, sends this length as a 32bit value, then streams the rest
 * -no HTTP header or other overhead, no disk I/o, it's length followed by data for easy use or chaining
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <time.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET sock_t;
  #define close_socket(s) closesocket(s)
  #define WRITE(s,b,n) send((s),(b),(n),0)
  static void winsock_init(void){ WSADATA d; WSAStartup(MAKEWORD(2,2), &d); }
  static void winsock_cleanup(void){ WSACleanup(); }
  #define THREAD_RET DWORD WINAPI
  #define THREAD_ARG  LPVOID
#else
  #include <unistd.h>
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <pthread.h>
  #include <syslog.h>
  typedef int sock_t;
  #define close_socket(s)	close(s)
  #define WRITE(s,b,n)	   write((s),(b),(n))
  static void winsock_init(void){ /* noop on Unix */ }
  static void winsock_cleanup(void){ /* noop on Unix */ }
  #define THREAD_RET void*
  #define THREAD_ARG  void*
#endif

#include <curl/curl.h>
#include "miniz.h"
#include <arpa/inet.h>  //htonl, inet_ntop

#define SERVER_PORT		38030
#define BACKLOG			32
#define CMD_BUF_LEN		256
#define MAX_EOCD_SEARCH	0x10000  //last 64KB

struct mem_range {//buffer type for partial HTTP range fetch
	unsigned char *data;
	size_t		 size;
};

static size_t curl_mem_cb(void *ptr, size_t sz, size_t nm, void *ud){//Curl write callback, accumulate into mem_range
	size_t len = sz*nm;
	struct mem_range *m = (struct mem_range*)ud;
	unsigned char *p = realloc(m->data, m->size + len);
	if(!p) return 0;
	m->data = p;
	memcpy(p + m->size, ptr, len);
	m->size += len;
	return len;
}
	
//fetch byte range [range] from URL into heap buffer
static int fetch_range(const char *url, const char *range, struct mem_range *out){
	CURL *c = curl_easy_init();
	if(!c) return 0;
	out->data = NULL;
	out->size = 0;
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_RANGE, range);  // e.g. "bytes=-65536"
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_mem_cb);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, out);
	CURLcode rc = curl_easy_perform(c);
	curl_easy_cleanup(c);
	return (rc == CURLE_OK);
}

#define CMD_MAX 1024

// Read up to (and including) a '\n' into out[], NUL-terminate.
// Returns number of bytes (including the '\n'), or –1 on error/EOF.
static int read_cmd_line(sock_t s, char *out, size_t maxlen){  //read until we buffer a \n, terminate string, return number of bytes
	size_t pos = 0;
	while(pos + 1 < maxlen){
		char c;
		int r = recv(s, &c, 1, 0);
		if(r <= 0) return -1;        // client closed or error
		out[pos++] = c;
		if(c == '\n') break;         // got the terminator
	}
	out[pos] = '\0';
	return (int)pos;
}

static uint16_t le16(const unsigned char *p){//parse little-endian 16bit from buffer
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const unsigned char *p){//parse little-endian 32bit from buffer
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

//discover uncompressed size by reading Central Directory
//returns 1 on success, fills *size_out, else 0.
static int get_uncompressed_size(const char *url, uint32_t *size_out){
	struct mem_range tail = {0};
	char range_hdr[32];

	//fetch last MAX_EOCD_SEARCH bytes
	snprintf(range_hdr, sizeof(range_hdr), "bytes=-%d", MAX_EOCD_SEARCH);
	if(!fetch_range(url, range_hdr, &tail)) return 0;
	if(tail.size < 22){ free(tail.data); return 0; }

	//find End-of-Central-Directory (EOCD) signature 0x06054b50
	ssize_t i;
	for(i = (ssize_t)tail.size - 22; i >= 0; i--){
		if(le32(tail.data + i) == 0x06054b50) break;
	}
	if(i < 0){ free(tail.data); return 0; }

	//read central directory size & offset
	uint32_t cd_size   = le32(tail.data + i + 12);
	uint32_t cd_offset = le32(tail.data + i + 16);
	free(tail.data);

	//fetch exactly the central directory
	struct mem_range cd = {0};
	snprintf(range_hdr, sizeof(range_hdr),
			 "bytes=%u-%u",
			 cd_offset, cd_offset + cd_size - 1);
	if(!fetch_range(url, range_hdr, &cd)) return 0;
	if(cd.size < 46){ free(cd.data); return 0; }

	//locate first Central Directory File Header (signature 0x02014b50)
	ssize_t j;
	for(j = 0; j + 4 < (ssize_t)cd.size; j++){//should start at cd.data[0], but let's be safe..
		if(le32(cd.data + j) == 0x02014b50) break;
	}
	if(j + 30 >= (ssize_t)cd.size){ free(cd.data); return 0; }

	//extract uncompressed size at offset j+24
	uint32_t uncomp = le32(cd.data + j + 24);
	free(cd.data);

	*size_out = uncomp;
	return 1;
}

static void urldecode(char *dst, const char *src){
	char hex[3] = {0};
	while(*src){
		if(*src=='%' && isxdigit(src[1]) && isxdigit(src[2])){
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

typedef struct{  //streaming decompress state from ZIP entry
	sock_t			  client;
	tinfl_decompressor  decomp;
	unsigned char	   in_buf[4096];
	size_t			  in_size;
	unsigned char	   header[64];
	size_t			  hdr_received;
	size_t			  hdr_needed;
	int				 state;  // 0 = header, 1 = decompress
} ctx_t;

static size_t extract_cb(void *opaque, mz_uint64 offset, const void *buf, size_t len){  //called by miniz to write decompressed bytes
	sock_t c = *(sock_t*)opaque;
	(void)offset;
	return WRITE(c, buf, len);
}

static size_t curl_cb(void *ptr, size_t sz, size_t nm, void *ud){  //curl write callback: feed ZIP bytes into header parse + tinfl
	size_t len = sz*nm;
	ctx_t *ctx = (ctx_t*)ud;
	size_t off = 0;

	//parse local file header on the fly
	if(ctx->state == 0){
		// first 30 bytes fixed
		if(ctx->hdr_received < 30){
			size_t want = 30 - ctx->hdr_received;
			size_t take = len < want ? len : want;
			memcpy(ctx->header + ctx->hdr_received, ptr+off, take);
			ctx->hdr_received += take;
			off += take;
			if(ctx->hdr_received == 30){
				uint16_t nlen = le16(ctx->header + 26);
				uint16_t elen = le16(ctx->header + 28);
				ctx->hdr_needed = 30 + nlen + elen;
			}
		}
		//filename+extra
		if(off < len && ctx->hdr_received < ctx->hdr_needed){
			size_t want = ctx->hdr_needed - ctx->hdr_received;
			size_t take = (len-off) < want ? (len-off) : want;
			memcpy(ctx->header + ctx->hdr_received, ptr+off, take);
			ctx->hdr_received += take;
			off += take;
		}
		if(ctx->hdr_received >= ctx->hdr_needed){
			tinfl_init(&ctx->decomp);
			ctx->state = 1;
			ctx->in_size = 0;
		}
	}

	//stream-decompress remainder
	if(ctx->state == 1 && off < len){
		size_t want = len - off;
		size_t room = sizeof(ctx->in_buf) - ctx->in_size;
		size_t take = want < room ? want : room;
		memcpy(ctx->in_buf + ctx->in_size, ptr+off, take);
		ctx->in_size += take;
		off += take;

		//decompress as far as possible
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
			//slide out consumed
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

static void get_peer_ip(sock_t s, char *buf, size_t buflen){
	struct sockaddr_storage ss;
	socklen_t len = sizeof(ss);
	getpeername(s, (struct sockaddr*)&ss, &len);
	if(ss.ss_family == AF_INET){
		inet_ntop(AF_INET,
				  &((struct sockaddr_in*)&ss)->sin_addr,
				  buf, buflen);
	}else{
		inet_ntop(AF_INET6,
				  &((struct sockaddr_in6*)&ss)->sin6_addr,
				  buf, buflen);
	}
}

static void handle_client_inner(sock_t client){
	char peer[INET6_ADDRSTRLEN] = {0};
	get_peer_ip(client, peer, sizeof(peer));
#ifndef _WIN32
	syslog(LOG_INFO, "Conn from %s", peer);
#endif

	//wait 4 seconds for "Unzip "
	fd_set rfds;
	struct timeval tv = {4,0};
	FD_ZERO(&rfds); FD_SET(client, &rfds);
	if(select((int)client+1, &rfds, NULL, NULL, &tv) <= 0){
#ifndef _WIN32
		syslog(LOG_WARNING, "Timeout from %s", peer);
#endif
		close_socket(client);
		return;
	}

	//read command
	char line[CMD_BUF_LEN];
	int n = recv(client, line, sizeof(line)-1, 0);
	if(n<=0){
#ifndef _WIN32
		syslog(LOG_WARNING, "Recv err from %s", peer);
#endif
		close_socket(client);
		return;
	}
	line[n] = 0;

	//check verb
	if(strncmp(line, "Unzip ", 6) != 0){
#ifndef _WIN32
		syslog(LOG_WARNING, "Bad cmd from %s: %.20s", peer, line);
#endif
		close_socket(client);
		return;
	}

	//decode URL
	char url_enc[1024], url[1024];
	strncpy(url_enc, line+6, sizeof(url_enc)-1);
	url_enc[sizeof(url_enc)-1] = 0;
	// strip newline
	for(int i=strlen(url_enc)-1; i>=0; i--){
		if(url_enc[i]=='\r'||url_enc[i]=='\n'||url_enc[i]==' ')
			url_enc[i]=0;
		else break;
	}
	urldecode(url, url_enc);

	//discover uncompressed size
	uint32_t uncomp;
	if(!get_uncompressed_size(url, &uncomp)){
#ifndef _WIN32
		syslog(LOG_ERR, "Size discovery failed for %s", peer);
#endif
		close_socket(client);
		return;
	}

	//send 32bit network order length
	uint32_t netlen = htonl(uncomp);
	WRITE(client, &netlen, sizeof(netlen));

	//stream-decompress the ZIP entry
	ctx_t ctx = {
		.client	   = client,
		.hdr_received = 0,
		.hdr_needed   = 30,
		.state		= 0,
		.in_size	  = 0
	};
	CURL *c = curl_easy_init();
	curl_easy_setopt(c, CURLOPT_URL,			url);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION,  curl_cb);
	curl_easy_setopt(c, CURLOPT_WRITEDATA,	  &ctx);
	curl_easy_perform(c);
	curl_easy_cleanup(c);

	close_socket(client);
}

#ifdef _WIN32
  static THREAD_RET client_thread(THREAD_ARG arg){
	  sock_t c = (sock_t)(uintptr_t)arg;
	  handle_client_inner(c);
	  return 0;
  }
#else
  static THREAD_RET client_thread(THREAD_ARG arg){
	  sock_t c = (sock_t)(uintptr_t)arg;
	  handle_client_inner(c);
	  return NULL;
  }
#endif

int main(){
	winsock_init();
	curl_global_init(CURL_GLOBAL_DEFAULT);
#ifndef _WIN32
	openlog("ZipStream", LOG_PID|LOG_NDELAY, LOG_DAEMON);
#endif

	sock_t srv = socket(AF_INET, SOCK_STREAM, 0);
	int on = 1;
	setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on));

	struct sockaddr_in addr = {.sin_family=AF_INET, .sin_addr.s_addr=INADDR_ANY, .sin_port=htons(SERVER_PORT)};
	bind(srv, (struct sockaddr*)&addr, sizeof(addr));
	listen(srv, BACKLOG);

	printf("Listening on port %d\n", SERVER_PORT);

	while(1){
		sock_t client = accept(srv, NULL, NULL);
		if(client < 0)
			continue;
	#ifdef _WIN32
		CreateThread(NULL,0,client_thread,(LPVOID)(uintptr_t)client,0,NULL);
	#else
		pthread_t tid;
		pthread_create(&tid, NULL, client_thread,
					   (void*)(uintptr_t)client);
		pthread_detach(tid);
	#endif
	}

	curl_global_cleanup();
	winsock_cleanup();
	return 0;
}
