#include "uzenet-ssh-server.h"
#include "uzenet-tunnel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <syslog.h>
#include <sys/wait.h>
#include <stdint.h>
#include <pthread.h>

// ----------------------------------------------------------------------------
// Config
// ----------------------------------------------------------------------------

// Socket used by uzenet-room -> ssh service
#define SSH_SOCKET_PATH "/run/uzenet/ssh.sock"
#define MAX_CLIENTS     32

// LOGIN meta payload (same as used in lichess / fatfs / virtual-fujinet)
typedef struct{
	uint16_t user_id;
	uint16_t reserved;
} TunnelLoginMeta;

// ----------------------------------------------------------------------------
// Globals
// ----------------------------------------------------------------------------

static int listener_fd = -1;

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

static void cleanup(void){
	if(listener_fd >= 0){
		close(listener_fd);
		unlink(SSH_SOCKET_PATH);
	}
}

static void handle_sigchld(int sig){
	(void)sig;
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

static void handle_sigpipe(int sig){
	(void)sig;
}

// Write all bytes to a stream fd (unframed side of socketpair)
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

// ----------------------------------------------------------------------------
// Per-session bridge
// ----------------------------------------------------------------------------

typedef struct{
	int ssh_fd;   // raw byte stream for uzenet_ssh_handle()
	int user_id;
} SSHThreadArgs;

// Runs uzenet_ssh_handle() on a plain stream fd
static void *ssh_thread_main(void *arg){
	SSHThreadArgs *st = (SSHThreadArgs*)arg;
	if(st){
		uzenet_ssh_handle(st->ssh_fd, st->user_id);
		close(st->ssh_fd);
		free(st);
	}
	return NULL;
}

typedef struct{
	int tunnel_fd;  // framed side (from uzenet-room)
} ClientThreadArgs;

// Thread per tunnel connection
static void *client_thread_main(void *arg){
	ClientThreadArgs *cta = (ClientThreadArgs*)arg;
	int tunnel_fd = cta->tunnel_fd;
	free(cta);

	int user_id = 0xFFFF;          // default "guest"
	TunnelFrame first_fr;
	int have_first_data = 0;

	// Expect first frame: LOGIN (preferred) or DATA
	int r = ReadTunnelFramed(tunnel_fd, &first_fr);
	if(r <= 0){
		close(tunnel_fd);
		return NULL;
	}

	if(first_fr.type == TUNNEL_TYPE_LOGIN &&
	   first_fr.length >= sizeof(TunnelLoginMeta)){
		const TunnelLoginMeta *meta = (const TunnelLoginMeta*)first_fr.data;
		user_id = meta->user_id;
		syslog(LOG_INFO, "uzenet-ssh: LOGIN user_id=%u",
		       (unsigned)user_id);
	}else if(first_fr.type == TUNNEL_TYPE_DATA && first_fr.length > 0){
		// Older room or no LOGIN meta: treat as initial data
		have_first_data = 1;
	}else{
		// Unknown frame type; continue as guest with no initial data
	}

	// Create local socketpair: ssh_fd <-> bridge_fd
	int sp[2];
	if(socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0){
		syslog(LOG_ERR, "uzenet-ssh: socketpair failed: %s",
		       strerror(errno));
		close(tunnel_fd);
		return NULL;
	}
	int ssh_fd   = sp[0]; // we keep this for bridging
	int bridge_fd= sp[1]; // this goes into uzenet_ssh_handle()

	// Start uzenet_ssh_handle() in its own thread
	SSHThreadArgs *st = (SSHThreadArgs*)malloc(sizeof(*st));
	if(!st){
		close(tunnel_fd);
		close(ssh_fd);
		close(bridge_fd);
		return NULL;
	}
	st->ssh_fd  = bridge_fd;
	st->user_id = user_id;

	pthread_t ssh_tid;
	if(pthread_create(&ssh_tid, NULL, ssh_thread_main, st) != 0){
		syslog(LOG_ERR, "uzenet-ssh: pthread_create failed: %s",
		       strerror(errno));
		free(st);
		close(tunnel_fd);
		close(ssh_fd);
		close(bridge_fd);
		return NULL;
	}
	pthread_detach(ssh_tid);

	// If first frame was DATA, push its payload into ssh_fd
	if(have_first_data && first_fr.length > 0){
		if(write_full_stream(ssh_fd, first_fr.data, first_fr.length) < 0){
			close(tunnel_fd);
			close(ssh_fd);
			return NULL;
		}
	}

	// Bridge loop: tunnel_fd <-> ssh_fd
	for(;;){
		fd_set rfds;
		int maxfd = (tunnel_fd > ssh_fd ? tunnel_fd : ssh_fd) + 1;

		FD_ZERO(&rfds);
		FD_SET(tunnel_fd, &rfds);
		FD_SET(ssh_fd,    &rfds);

		int n = select(maxfd, &rfds, NULL, NULL, NULL);
		if(n < 0){
			if(errno == EINTR) continue;
			break;
		}

		// Tunnel -> SSH
		if(FD_ISSET(tunnel_fd, &rfds)){
			TunnelFrame fr;
			int rt = ReadTunnelFramed(tunnel_fd, &fr);
			if(rt <= 0){
				// Remote closed or error; stop sending to ssh
				shutdown(ssh_fd, SHUT_WR);
				break;
			}
			if(fr.type == TUNNEL_TYPE_DATA && fr.length > 0){
				if(write_full_stream(ssh_fd, fr.data, fr.length) < 0){
					shutdown(ssh_fd, SHUT_WR);
					break;
				}
			}
		}

		// SSH -> Tunnel
		if(FD_ISSET(ssh_fd, &rfds)){
			uint8_t buf[1024];
			ssize_t rd = read(ssh_fd, buf, sizeof(buf));
			if(rd <= 0){
				// ssh side closed; we're done
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

	close(ssh_fd);
	close(tunnel_fd);
	return NULL;
}

// ----------------------------------------------------------------------------
// main()
// ----------------------------------------------------------------------------

int main(void){
	openlog("uzenet-ssh", LOG_PID | LOG_CONS, LOG_DAEMON);

	signal(SIGCHLD, handle_sigchld);
	signal(SIGPIPE, handle_sigpipe);
	atexit(cleanup);

	uzenet_ssh_server_init();

	// Set up Unix domain socket listener
	listener_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if(listener_fd < 0){
		syslog(LOG_ERR, "socket failed: %s", strerror(errno));
		exit(1);
	}

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, SSH_SOCKET_PATH, sizeof(addr.sun_path) - 1);
	unlink(SSH_SOCKET_PATH); // Ensure it's gone
	if(bind(listener_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
		syslog(LOG_ERR, "bind failed: %s", strerror(errno));
		exit(1);
	}

	if(listen(listener_fd, MAX_CLIENTS) < 0){
		syslog(LOG_ERR, "listen failed: %s", strerror(errno));
		exit(1);
	}

	syslog(LOG_INFO, "uzenet-ssh listening on %s", SSH_SOCKET_PATH);

	// Accept framed connections from uzenet-room
	for(;;){
		int client_fd = accept(listener_fd, NULL, NULL);
		if(client_fd < 0){
			if(errno == EINTR) continue;
			syslog(LOG_ERR, "accept failed: %s", strerror(errno));
			continue;
		}

		ClientThreadArgs *cta = (ClientThreadArgs*)malloc(sizeof(*cta));
		if(!cta){
			close(client_fd);
			continue;
		}
		cta->tunnel_fd = client_fd;

		pthread_t tid;
		if(pthread_create(&tid, NULL, client_thread_main, cta) != 0){
			syslog(LOG_ERR, "pthread_create failed: %s", strerror(errno));
			close(client_fd);
			free(cta);
			continue;
		}
		pthread_detach(tid);
	}
}
