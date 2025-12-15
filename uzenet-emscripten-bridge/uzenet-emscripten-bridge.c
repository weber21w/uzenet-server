#define _GNU_SOURCE

#include "uzenet-emscripten-bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>

typedef struct uemb_vsock {
	int		fd;			/* outbound UDS fd, -1 if unused */
	int		open;		/* 1 if open */
} uemb_vsock_t;

typedef struct uemb_client {
	int		fd;			/* client TCP fd */
	int		alive;

	/* input assembly */
	uint8_t	rx[UEMB_RXBUF];
	int		rx_len;

	/* token bucket */
	int		tokens;
	uint32_t	last_ms;

	uemb_vsock_t	vs[UEMB_MAX_VSOCKS];
} uemb_client_t;

static uint32_t uemb_now_ms(void){
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

static int uemb_set_nonblock(int fd){
	int fl = fcntl(fd, F_GETFL, 0);
	if(fl < 0) return -1;
	if(fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) return -1;
	return 0;
}

static int uemb_send_frame(int fd, uint8_t type, const void *payload, int payload_len){
	uint8_t hdr[3];
	uint16_t len = (uint16_t)(1 + payload_len); /* type + payload */
	if(len == 0 || len > UEMB_MAX_FRAME) return -1;

	hdr[0] = (uint8_t)((len >> 8) & 0xFF);
	hdr[1] = (uint8_t)(len & 0xFF);
	hdr[2] = type;

	/* writev would be nicer; keep simple */
	if(send(fd, (const char*)hdr, 3, 0) != 3) return -1;
	if(payload_len > 0){
		if(send(fd, (const char*)payload, payload_len, 0) != payload_len) return -1;
	}
	return 0;
}

/* hostmap: very small parser
 * lines:
 *	host uds /run/uzenet/uzenet-room.sock
 *	# comment
 */
static int uemb_load_hostmap(const char *path, uemb_hostmap_entry_t *out, int max_out){
	FILE *f = fopen(path, "r");
	if(!f) return -1;

	char line[512];
	int n = 0;

	while(fgets(line, sizeof(line), f)){
		char host[UEMB_MAX_HOSTNAME];
		char type[16];
		char target[UEMB_MAX_UDS_PATH];

		char *p = line;
		while(*p == ' ' || *p == '\t') p++;
		if(*p == 0 || *p == '\n' || *p == '#') continue;

		host[0] = 0;
		type[0] = 0;
		target[0] = 0;

		if(sscanf(p, "%63s %15s %107s", host, type, target) != 3)
			continue;

		if(strcmp(type, "uds") != 0)
			continue;

		if(n < max_out){
			memset(&out[n], 0, sizeof(out[n]));
			snprintf(out[n].host, sizeof(out[n].host), "%s", host);
			snprintf(out[n].uds_path, sizeof(out[n].uds_path), "%s", target);
			n++;
		}
	}

	fclose(f);
	return n;
}

static const char* uemb_host_to_uds(const uemb_hostmap_entry_t *hm, int hm_n, const char *host){
	for(int i=0;i<hm_n;i++){
		if(strcmp(hm[i].host, host) == 0)
			return hm[i].uds_path;
	}
	return NULL;
}

static int uemb_connect_uds(const char *path){
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if(fd < 0) return -1;

	struct sockaddr_un sa;
	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", path);

	if(connect(fd, (struct sockaddr*)&sa, (socklen_t)sizeof(sa)) < 0){
		close(fd);
		return -1;
	}

	uemb_set_nonblock(fd);
	return fd;
}

static void uemb_client_close(uemb_client_t *c){
	if(!c->alive) return;

	for(int i=0;i<UEMB_MAX_VSOCKS;i++){
		if(c->vs[i].fd >= 0){
			close(c->vs[i].fd);
			c->vs[i].fd = -1;
			c->vs[i].open = 0;
		}
	}

	close(c->fd);
	c->fd = -1;
	c->alive = 0;
	c->rx_len = 0;
}

static void uemb_refill_tokens(uemb_client_t *c, int rate_per_sec, int burst){
	uint32_t now = uemb_now_ms();
	uint32_t dt = now - c->last_ms;

	c->last_ms = now;

	/* add tokens proportional to time */
	int add = (int)(((uint64_t)dt * (uint64_t)rate_per_sec) / 1000u);
	if(add > 0){
		c->tokens += add;
		if(c->tokens > burst)
			c->tokens = burst;
	}
}

static int uemb_take_tokens(uemb_client_t *c, int nbytes){
	if(nbytes <= 0) return 1;
	if(c->tokens < nbytes) return 0;
	c->tokens -= nbytes;
	return 1;
}

static void uemb_handle_open(uemb_client_t *c, const uemb_hostmap_entry_t *hm, int hm_n, const uint8_t *p, int n){
	/* payload: u8 vsock_id, u8 host_len, host bytes */
	if(n < 2){
		uint8_t rep[3] = { 0, 0, 0 };
		(void)rep;
		return;
	}

	uint8_t vsid = p[0];
	uint8_t hl = p[1];

	if(vsid >= UEMB_MAX_VSOCKS || hl == 0 || hl > UEMB_MAX_HOSTNAME-1 || (2 + hl) > (uint8_t)n){
		int16_t err = UEMB_ERR_BADREQ;
		uint8_t payload[3];
		payload[0] = vsid;
		payload[1] = (uint8_t)((err >> 8) & 0xFF);
		payload[2] = (uint8_t)(err & 0xFF);
		uemb_send_frame(c->fd, UEMB_S_OPEN_FAIL, payload, 3);
		return;
	}

	char host[UEMB_MAX_HOSTNAME];
	memset(host, 0, sizeof(host));
	memcpy(host, &p[2], hl);
	host[hl] = 0;

	const char *uds = uemb_host_to_uds(hm, hm_n, host);
	if(!uds){
		int16_t err = UEMB_ERR_DENIED;
		uint8_t payload[3];
		payload[0] = vsid;
		payload[1] = (uint8_t)((err >> 8) & 0xFF);
		payload[2] = (uint8_t)(err & 0xFF);
		uemb_send_frame(c->fd, UEMB_S_OPEN_FAIL, payload, 3);
		syslog(LOG_NOTICE, "deny open host=%s", host);
		return;
	}

	/* close existing */
	if(c->vs[vsid].fd >= 0){
		close(c->vs[vsid].fd);
		c->vs[vsid].fd = -1;
		c->vs[vsid].open = 0;
	}

	int fd = uemb_connect_uds(uds);
	if(fd < 0){
		int16_t err = UEMB_ERR_CONNECT;
		uint8_t payload[3];
		payload[0] = vsid;
		payload[1] = (uint8_t)((err >> 8) & 0xFF);
		payload[2] = (uint8_t)(err & 0xFF);
		uemb_send_frame(c->fd, UEMB_S_OPEN_FAIL, payload, 3);
		syslog(LOG_NOTICE, "open fail host=%s uds=%s err=%d", host, uds, errno);
		return;
	}

	c->vs[vsid].fd = fd;
	c->vs[vsid].open = 1;

	{
		uint8_t payload[1] = { vsid };
		uemb_send_frame(c->fd, UEMB_S_OPEN_OK, payload, 1);
	}

	syslog(LOG_INFO, "open ok host=%s vsock=%u uds=%s", host, vsid, uds);
}

static void uemb_handle_send(uemb_client_t *c, const uint8_t *p, int n){
	/* payload: u8 vsock_id, data... */
	if(n < 1) return;

	uint8_t vsid = p[0];
	if(vsid >= UEMB_MAX_VSOCKS) return;

	if(!c->vs[vsid].open || c->vs[vsid].fd < 0) return;

	int dlen = n - 1;
	if(dlen <= 0) return;

	/* note: for now, drop if would block */
	int r = (int)send(c->vs[vsid].fd, (const char*)&p[1], dlen, 0);
	(void)r;
}

static void uemb_handle_close(uemb_client_t *c, const uint8_t *p, int n){
	/* payload: u8 vsock_id */
	if(n < 1) return;

	uint8_t vsid = p[0];
	if(vsid >= UEMB_MAX_VSOCKS) return;

	if(c->vs[vsid].fd >= 0){
		close(c->vs[vsid].fd);
		c->vs[vsid].fd = -1;
		c->vs[vsid].open = 0;
	}

	{
		int16_t reason = UEMB_ERR_OK;
		uint8_t payload[3];
		payload[0] = vsid;
		payload[1] = (uint8_t)((reason >> 8) & 0xFF);
		payload[2] = (uint8_t)(reason & 0xFF);
		uemb_send_frame(c->fd, UEMB_S_CLOSED, payload, 3);
	}
}

static void uemb_process_frames(uemb_client_t *c, const uemb_hostmap_entry_t *hm, int hm_n){
	/* parse as many complete frames as we have */
	int off = 0;

	while(1){
		if((c->rx_len - off) < 2) break;

		uint16_t len = ((uint16_t)c->rx[off] << 8) | (uint16_t)c->rx[off + 1];
		if(len == 0 || len > UEMB_MAX_FRAME){
			uemb_client_close(c);
			return;
		}
		if((c->rx_len - off) < (2 + (int)len)) break;

		uint8_t type = c->rx[off + 2];
		const uint8_t *pl = &c->rx[off + 3];
		int pl_len = (int)len - 1;

		/* rate limit based on inbound payload bytes (approx) */
		if(!uemb_take_tokens(c, (int)len)){
			/* drop frame if over budget */
			off += 2 + (int)len;
			continue;
		}

		switch(type){
			case UEMB_C_OPEN:		uemb_handle_open(c, hm, hm_n, pl, pl_len); break;
			case UEMB_C_SEND:		uemb_handle_send(c, pl, pl_len); break;
			case UEMB_C_CLOSE:		uemb_handle_close(c, pl, pl_len); break;
			default: break;
		}

		off += 2 + (int)len;
	}

	/* compact */
	if(off > 0){
		memmove(c->rx, &c->rx[off], (size_t)(c->rx_len - off));
		c->rx_len -= off;
	}
}

static int uemb_listen_tcp(const char *ip, int port){
	int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(fd < 0) return -1;

	int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, (socklen_t)sizeof(one));

	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((uint16_t)port);
	if(!ip) ip = "127.0.0.1";
	if(inet_pton(AF_INET, ip, &sa.sin_addr) != 1){
		close(fd);
		return -1;
	}

	if(bind(fd, (struct sockaddr*)&sa, (socklen_t)sizeof(sa)) < 0){
		close(fd);
		return -1;
	}
	if(listen(fd, 16) < 0){
		close(fd);
		return -1;
	}

	uemb_set_nonblock(fd);
	return fd;
}

int uemb_run(const uemb_config_t *cfg){
	uemb_config_t c0;
	memset(&c0, 0, sizeof(c0));

	c0.listen_ip = "127.0.0.1";
	c0.listen_port = 38081;
	c0.hostmap_path = "uzenet-hostmap.cfg";
	c0.max_clients = UEMB_MAX_CLIENTS;
	c0.max_vsocks = UEMB_MAX_VSOCKS;
	c0.rate_bytes_per_sec = 256 * 1024;
	c0.rate_burst_bytes = 256 * 1024;

	if(cfg){
		if(cfg->listen_ip) c0.listen_ip = cfg->listen_ip;
		if(cfg->listen_port) c0.listen_port = cfg->listen_port;
		if(cfg->hostmap_path) c0.hostmap_path = cfg->hostmap_path;
		if(cfg->max_clients) c0.max_clients = cfg->max_clients;
		if(cfg->max_vsocks) c0.max_vsocks = cfg->max_vsocks;
		if(cfg->rate_bytes_per_sec) c0.rate_bytes_per_sec = cfg->rate_bytes_per_sec;
		if(cfg->rate_burst_bytes) c0.rate_burst_bytes = cfg->rate_burst_bytes;
	}

	openlog("uzenet-emscripten-bridge", LOG_PID | LOG_NDELAY, LOG_DAEMON);

	uemb_hostmap_entry_t hostmap[128];
	int hostmap_n = uemb_load_hostmap(c0.hostmap_path, hostmap, (int)(sizeof(hostmap)/sizeof(hostmap[0])));
	if(hostmap_n < 0){
		syslog(LOG_ERR, "failed to load hostmap: %s", c0.hostmap_path);
		return 1;
	}
	syslog(LOG_INFO, "loaded hostmap entries=%d", hostmap_n);

	int lfd = uemb_listen_tcp(c0.listen_ip, c0.listen_port);
	if(lfd < 0){
		syslog(LOG_ERR, "listen failed %s:%d err=%d", c0.listen_ip, c0.listen_port, errno);
		return 1;
	}

	syslog(LOG_INFO, "listening tcp %s:%d", c0.listen_ip, c0.listen_port);

	uemb_client_t clients[UEMB_MAX_CLIENTS];
	memset(clients, 0, sizeof(clients));
	for(int i=0;i<UEMB_MAX_CLIENTS;i++){
		clients[i].fd = -1;
		clients[i].alive = 0;
		clients[i].rx_len = 0;
		clients[i].tokens = c0.rate_burst_bytes;
		clients[i].last_ms = uemb_now_ms();
		for(int j=0;j<UEMB_MAX_VSOCKS;j++){
			clients[i].vs[j].fd = -1;
			clients[i].vs[j].open = 0;
		}
	}

	while(1){
		/* build poll list */
		struct pollfd pfds[1 + UEMB_MAX_CLIENTS + (UEMB_MAX_CLIENTS * UEMB_MAX_VSOCKS)];
		int pcount = 0;

		pfds[pcount].fd = lfd;
		pfds[pcount].events = POLLIN;
		pfds[pcount].revents = 0;
		pcount++;

		for(int ci=0;ci<UEMB_MAX_CLIENTS;ci++){
			if(!clients[ci].alive) continue;

			/* refill token bucket */
			uemb_refill_tokens(&clients[ci], c0.rate_bytes_per_sec, c0.rate_burst_bytes);

			pfds[pcount].fd = clients[ci].fd;
			pfds[pcount].events = POLLIN;
			pfds[pcount].revents = 0;
			pcount++;

			for(int vi=0;vi<UEMB_MAX_VSOCKS;vi++){
				if(clients[ci].vs[vi].fd < 0) continue;

				pfds[pcount].fd = clients[ci].vs[vi].fd;
				pfds[pcount].events = POLLIN;
				pfds[pcount].revents = 0;
				pcount++;
			}
		}

		int r = poll(pfds, (nfds_t)pcount, 20);
		if(r < 0){
			if(errno == EINTR) continue;
			syslog(LOG_ERR, "poll err=%d", errno);
			break;
		}

		/* accept new */
		if(pfds[0].revents & POLLIN){
			struct sockaddr_in sa;
			socklen_t sl = (socklen_t)sizeof(sa);
			int cfd = accept(lfd, (struct sockaddr*)&sa, &sl);
			if(cfd >= 0){
				uemb_set_nonblock(cfd);

				/* find slot */
				int placed = 0;
				for(int ci=0;ci<UEMB_MAX_CLIENTS;ci++){
					if(!clients[ci].alive){
						clients[ci].fd = cfd;
						clients[ci].alive = 1;
						clients[ci].rx_len = 0;
						clients[ci].tokens = c0.rate_burst_bytes;
						clients[ci].last_ms = uemb_now_ms();
						for(int vi=0;vi<UEMB_MAX_VSOCKS;vi++){
							clients[ci].vs[vi].fd = -1;
							clients[ci].vs[vi].open = 0;
						}
						placed = 1;
						syslog(LOG_INFO, "client accepted slot=%d", ci);
						break;
					}
				}
				if(!placed){
					close(cfd);
					syslog(LOG_NOTICE, "client rejected (no slots)");
				}
			}
		}

		/* handle IO; we need a stable mapping back to which pollfd belongs to what.
		 * simplest: re-walk and consume pollfd indices in the same order we built them.
		 */
		int pi = 1;

		for(int ci=0;ci<UEMB_MAX_CLIENTS;ci++){
			uemb_client_t *cl = &clients[ci];
			if(!cl->alive) continue;

			/* client fd */
			if(pi >= pcount) break;

			if(pfds[pi].revents & (POLLHUP | POLLERR)){
				uemb_client_close(cl);
				pi++;
				/* still must skip vsocks in this walk; easiest: close already drops vsocks, but poll list still includes them.
				 * we keep consuming indices, ignoring.
				 */
				for(int vi=0;vi<UEMB_MAX_VSOCKS;vi++){
					if(cl->vs[vi].fd >= 0) pi++;
				}
				continue;
			}

			if(pfds[pi].revents & POLLIN){
				int space = UEMB_RXBUF - cl->rx_len;
				if(space > 0){
					int n = (int)recv(cl->fd, (char*)&cl->rx[cl->rx_len], space, 0);
					if(n <= 0){
						uemb_client_close(cl);
					}else{
						cl->rx_len += n;
						uemb_process_frames(cl, hostmap, hostmap_n);
					}
				}else{
					/* overflow -> drop client */
					uemb_client_close(cl);
				}
			}
			pi++;

			/* vsocks */
			for(int vi=0;vi<UEMB_MAX_VSOCKS;vi++){
				if(pi >= pcount) break;
				if(cl->vs[vi].fd < 0) continue;

				if(pfds[pi].revents & (POLLHUP | POLLERR)){
					/* notify closed */
					close(cl->vs[vi].fd);
					cl->vs[vi].fd = -1;
					cl->vs[vi].open = 0;

					int16_t reason = UEMB_ERR_IO;
					uint8_t payload[3];
					payload[0] = (uint8_t)vi;
					payload[1] = (uint8_t)((reason >> 8) & 0xFF);
					payload[2] = (uint8_t)(reason & 0xFF);
					uemb_send_frame(cl->fd, UEMB_S_CLOSED, payload, 3);
				}else if(pfds[pi].revents & POLLIN){
					uint8_t buf[1500];
					int n = (int)recv(cl->vs[vi].fd, (char*)buf, (int)sizeof(buf), 0);
					if(n <= 0){
						close(cl->vs[vi].fd);
						cl->vs[vi].fd = -1;
						cl->vs[vi].open = 0;

						int16_t reason = UEMB_ERR_IO;
						uint8_t payload[3];
						payload[0] = (uint8_t)vi;
						payload[1] = (uint8_t)((reason >> 8) & 0xFF);
						payload[2] = (uint8_t)(reason & 0xFF);
						uemb_send_frame(cl->fd, UEMB_S_CLOSED, payload, 3);
					}else{
						/* forward RX */
						uint8_t out[1 + 1500];
						out[0] = (uint8_t)vi;
						memcpy(&out[1], buf, (size_t)n);
						uemb_send_frame(cl->fd, UEMB_S_RX, out, 1 + n);
					}
				}
				pi++;
			}
		}
	}

	closelog();
	return 0;
}

/* optional tiny main if you want it standalone */
#ifdef UEMB_STANDALONE_MAIN
int main(int argc, char **argv){
	uemb_config_t cfg;
	memset(&cfg, 0, sizeof(cfg));
	cfg.listen_ip = "127.0.0.1";
	cfg.listen_port = 38081;
	cfg.hostmap_path = (argc > 1) ? argv[1] : "uzenet-hostmap.cfg";
	return uemb_run(&cfg);
}
#endif