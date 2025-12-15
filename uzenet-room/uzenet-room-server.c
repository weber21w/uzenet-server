#define _GNU_SOURCE
#include "uzenet-room-server.h"
#include "../uzenet-identity/uzenet-identity-client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <syslog.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/un.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#define PORT 9470
#define MAX_CLIENTS 64
#define IDENTITY_TIMEOUT_US 3000000
#define IDENTITY_PATH "/run/uzenet/identity.sock"
#define CERT_FILE "/etc/uzenet/server.crt"
#define KEY_FILE "/etc/uzenet/server.key"

typedef struct {
	int fd;
	SSL *ssl;
	int using_tls;
	struct sockaddr_in addr;
	pthread_t thread;
	char ip[64];
	struct uzenet_identity ident;
	int flow_hold;
	uint64_t tokens;
	uint64_t last_refill;
	uint64_t last_activity_us;
	struct service_tunnel {
		uint8_t queue[4096];
		int head, tail;
	} tunnels[MAX_SERVICE_TUNNELS];
} client_t;

static client_t clients[MAX_CLIENTS];
static volatile int quitting = 0;
static SSL_CTX *tls_ctx = NULL;

static void signal_handler(int sig){
	quitting = 1;
}

static uint64_t now_us(){
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
}

static void refill_tokens(client_t *c){
	uint64_t now = now_us();
	uint64_t elapsed = now - c->last_refill;
	if(elapsed > 0){
		c->tokens += elapsed / 100;
		if(c->tokens > 65536) c->tokens = 65536;
		c->last_refill = now;
	}
}

static int send_data(client_t *c, const void *buf, size_t len){
	refill_tokens(c);
	if(c->tokens < len && c->flow_hold) return 0;
	if(c->tokens < len) usleep((len - c->tokens) * 100);
	c->tokens -= len;
	if(c->using_tls) return SSL_write(c->ssl, buf, len);
	else return send(c->fd, buf, len, 0);
}

static int recv_data(client_t *c, void *buf, size_t len){
	if(c->using_tls) return SSL_read(c->ssl, buf, len);
	else return recv(c->fd, buf, len, 0);
}

static void queue_tunnel(client_t *c, int tunnel, const uint8_t *data, int len){
	struct service_tunnel *t = &c->tunnels[tunnel];
	for(int i = 0; i < len; ++i){
		int next = (t->head + 1) % sizeof(t->queue);
		if(next == t->tail) break;
		t->queue[t->head] = data[i];
		t->head = next;
	}
}

static void flush_tunnels(client_t *c){
	for(int i = 0; i < MAX_SERVICE_TUNNELS; ++i){
		struct service_tunnel *t = &c->tunnels[i];
		if(t->tail != t->head){
			uint8_t frame[260];
			int len = 0;
			frame[len++] = 0xF0 | (i & 0x0F);
			frame[len++] = 0;
			int p = t->tail;
			while(p != t->head && len < 256 + 2){
				frame[len++] = t->queue[p];
				p = (p + 1) % sizeof(t->queue);
			}
			t->tail = p;
			frame[1] = len - 2;
			send_data(c, frame, len);
		}
	}
}

static int do_login(client_t *c){
	uint8_t pw[6];
	int got = 0;
	uint64_t start = now_us();
	while(got < 6){
		uint8_t b;
		int r = recv_data(c, &b, 1);
		if(r == 1){
			pw[got++] = b;
		}else if(r <= 0){
			if(now_us() - start > IDENTITY_TIMEOUT_US) return 0;
			usleep(1000);
		}
	}

	int sock = socket(AF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	strcpy(addr.sun_path, IDENTITY_PATH);
	if(connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0){
		close(sock);
		return 0;
	}
	send(sock, pw, 6, 0);
	uint8_t reply[2];
	int r = recv(sock, reply, 2, MSG_WAITALL);
	close(sock);
	if(r != 2) return 0;

	uint16_t uid = (reply[0] << 8) | reply[1];
	c->ident.user_id = uid;
	snprintf(c->ident.name13, 14, "%06u", (uid == 0xFFFF ? 0 : uid));
	snprintf(c->ident.name8, 9,  "%s", c->ident.name13);
	snprintf(c->ident.name6, 7,  "%s", c->ident.name13);
	c->ident.flags = (uid == 0xFFFF ? 'R' : 'G');
	return 1;
}

static void *client_thread(void *arg){
	client_t *c = (client_t *)arg;
	uint8_t buf[1024];
	syslog(LOG_INFO, "room: connected from %s (%s)", c->ip, c->using_tls ? "TLS" : "plain");

	if(!do_login(c)){
		syslog(LOG_WARNING, "room: failed login from %s", c->ip);
		if(c->using_tls && c->ssl) SSL_free(c->ssl);
		close(c->fd);
		c->fd = -1;
		return NULL;
	}

	syslog(LOG_INFO, "room: user %s (id %04x) logged in", c->ident.name13, c->ident.user_id);
	c->last_activity_us = now_us();

	while(!quitting){
		int r = recv_data(c, buf, sizeof(buf));
		if(r == 0 || (r < 0 && (!c->using_tls || SSL_get_error(c->ssl, r) != SSL_ERROR_WANT_READ))) break;
		if(r > 0){
			c->last_activity_us = now_us();
			for(int i = 0; i < r; ++i){
				uint8_t cmd = buf[i];
				if(cmd == 0xFF){
					c->flow_hold = 1;
				}else if(cmd == 0xFE){
					c->flow_hold = 0;
				}else if((cmd & 0xF0) == 0xF0){
					int tunnel = cmd & 0x0F;
					if(i + 1 >= r) break;
					int len = buf[++i];
					if(i + len >= r) break;
					queue_tunnel(c, tunnel, &buf[i + 1], len);
					i += len;
				}else{
					// TODO: room command dispatch
				}
			}
		}
		if(now_us() - c->last_activity_us > 30000000ULL) break;
		flush_tunnels(c);
		usleep(1000);
	}
	syslog(LOG_INFO, "room: user %s disconnected", c->ident.name13);
	if(c->using_tls && c->ssl) SSL_free(c->ssl);
	close(c->fd);
	c->fd = -1;
	return NULL;
}

int main(){
	signal(SIGINT, signal_handler);
	openlog("uzenet-room", LOG_PID | LOG_NDELAY, LOG_DAEMON);

	SSL_library_init();
	OpenSSL_add_all_algorithms();
	SSL_load_error_strings();
	tls_ctx = SSL_CTX_new(TLS_server_method());
	if(!tls_ctx ||
	   !SSL_CTX_use_certificate_file(tls_ctx, CERT_FILE, SSL_FILETYPE_PEM) ||
	   !SSL_CTX_use_PrivateKey_file(tls_ctx, KEY_FILE, SSL_FILETYPE_PEM)){
		ERR_print_errors_fp(stderr);
		exit(1);
	}

	int sock = socket(AF_INET, SOCK_STREAM, 0);
	int opt = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(PORT),
		.sin_addr.s_addr = INADDR_ANY,
	};
	if(bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0){
		perror("bind");
		exit(1);
	}
	if(listen(sock, 16) < 0){
		perror("listen");
		exit(1);
	}

	printf("Uzenet Room Server listening on port %d (TLS+plain)\n", PORT);

	for(int i = 0; i < MAX_CLIENTS; ++i)
		clients[i].fd = -1;

	while(!quitting){
		struct sockaddr_in cli;
		socklen_t slen = sizeof(cli);
		int fd = accept(sock, (struct sockaddr *)&cli, &slen);
		if(fd < 0){
			if(errno == EINTR) break;
			continue;
		}

		uint8_t peek;
		recv(fd, &peek, 1, MSG_PEEK);

		for(int i = 0; i < MAX_CLIENTS; ++i){
			if(clients[i].fd < 0){
				clients[i].fd = fd;
				clients[i].addr = cli;
				snprintf(clients[i].ip, sizeof(clients[i].ip), "%s", inet_ntoa(cli.sin_addr));
				struct timeval now;
				gettimeofday(&now, NULL);
				clients[i].last_refill = (uint64_t)now.tv_sec * 1000000ULL + now.tv_usec;
				clients[i].tokens = 65536;
				clients[i].using_tls = (peek == 0x16);
				clients[i].ssl = NULL;
				if(clients[i].using_tls){
					SSL *ssl = SSL_new(tls_ctx);
					SSL_set_fd(ssl, fd);
					if(SSL_accept(ssl) <= 0){
						ERR_print_errors_fp(stderr);
						close(fd);
						clients[i].fd = -1;
						continue;
					}
					clients[i].ssl = ssl;
				}
				pthread_create(&clients[i].thread, NULL, client_thread, &clients[i]);
				break;
			}
		}
	}

	close(sock);
	closelog();
	return 0;
}
