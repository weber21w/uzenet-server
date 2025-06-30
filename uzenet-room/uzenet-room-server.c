// uzenet-room-server.c - Modular Uzenet room server
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

#define PORT 9470
#define MAX_CLIENTS 64

typedef struct {
	int fd;
	struct sockaddr_in addr;
	pthread_t thread;
	char ip[64];
	struct uzenet_identity ident;
	int flow_hold;
	uint64_t tokens;
	uint64_t last_refill;
	struct service_tunnel {
		uint8_t queue[4096];
		int head, tail;
	} tunnels[MAX_SERVICE_TUNNELS];
} client_t;

static client_t clients[MAX_CLIENTS];

static volatile int quitting = 0;

static void signal_handler(int sig){
	quitting = 1;
}

static void refill_tokens(client_t *c){
	struct timeval now;
	gettimeofday(&now, NULL);
	uint64_t now_us = (uint64_t)now.tv_sec * 1000000ULL + now.tv_usec;
	uint64_t elapsed = now_us - c->last_refill;
	if(elapsed > 0){
		c->tokens += elapsed / 100; // refill at 10k tokens/sec
		if(c->tokens > 65536) c->tokens = 65536;
		c->last_refill = now_us;
	}
}

static int send_data(client_t *c, const void *buf, size_t len){
	refill_tokens(c);
	if(c->tokens < len && c->flow_hold)
		return 0;
	if(c->tokens < len)
		usleep((len - c->tokens) * 100); // delay until enough tokens

	c->tokens -= len;
	return send(c->fd, buf, len, 0);
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
			frame[len++] = 0; // placeholder for length

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

static void *client_thread(void *arg){
	client_t *c = (client_t *)arg;
	uint8_t buf[1024];
	syslog(LOG_INFO, "room: connected from %s", c->ip);

	if(!uzenet_identity_check_fd(c->fd, &c->ident)){
		syslog(LOG_WARNING, "room: failed login from %s", c->ip);
		close(c->fd);
		c->fd = -1;
		return NULL;
	}

	syslog(LOG_INFO, "room: user %s logged in", c->ident.name13);

	while(!quitting){
		int r = recv(c->fd, buf, sizeof(buf), MSG_DONTWAIT);
		if(r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) break;
		if(r > 0){
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
		flush_tunnels(c);
		usleep(1000);
	}
	syslog(LOG_INFO, "room: user %s disconnected", c->ident.name13);
	close(c->fd);
	c->fd = -1;
	return NULL;
}

int main(){
	signal(SIGINT, signal_handler);
	openlog("uzenet-room", LOG_PID | LOG_NDELAY, LOG_DAEMON);
	uzenet_identity_init();

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

	printf("Uzenet Room Server listening on port %d\n", PORT);

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

		for(int i = 0; i < MAX_CLIENTS; ++i){
			if(clients[i].fd < 0){
				clients[i].fd = fd;
				clients[i].addr = cli;
				snprintf(clients[i].ip, sizeof(clients[i].ip), "%s", inet_ntoa(cli.sin_addr));
				struct timeval now;
				gettimeofday(&now, NULL);
				clients[i].last_refill = (uint64_t)now.tv_sec * 1000000ULL + now.tv_usec;
				clients[i].tokens = 65536;
				pthread_create(&clients[i].thread, NULL, client_thread, &clients[i]);
				break;
			}
		}
	}

	close(sock);
	closelog();
	return 0;
}
