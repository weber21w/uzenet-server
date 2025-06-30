/*  uzenet-lynx-server.c
 *  Simple multi-user Lynx proxy for Uzebox/Uzenet
 *  Build:  gcc -O2 -pthread -Wall uzenet-lynx-server.c -o uzenet-lynx-server
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <pty.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <termios.h>
#include <unistd.h>

/* ─────────────────────────────── Config ──────────────────────────────── */
#define LISTEN_PORT              57429
#define BACKLOG                  16
#define MAX_USERS                32
#define MAX_NAME_LEN             32
#define COLS                     80
#define ROWS                     25
#define HANDSHAKE_STR            "UZENETLYNX"
#define HANDSHAKE_LEN            10
#define ROOT_DIR                 "/srv/uzenet/lynx"   /* per-user homes */

/* client → server cmds */
#define CMD_LOGIN                0x00
#define CMD_KEY                  0x01

/* server → client pkts (MSB set to avoid clash with cmds) */
#define PKT_ROW                  0x80
#define PKT_CURSOR               0x81

/* ─────────────────────────────── Helpers ─────────────────────────────── */
static void log_msg(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	openlog("uzenet_lynx", LOG_PID, LOG_LOCAL6);
	vsyslog(LOG_INFO, fmt, ap);
	closelog();
	va_end(ap);
}

/* write all, handle EINTR */
static int xwrite(int fd, const void *buf, size_t len)
{
	const uint8_t *p = buf;
	while(len){
		ssize_t w = write(fd, p, len);
		if(w < 0){
			if(errno == EINTR) continue;
			return -1;
		}
		p += w; len -= w;
	}
	return 0;
}

/* ─────────────────────────── Session struct ──────────────────────────── */
typedef struct {
	int				sock_fd;
	int				pty_fd;
	pid_t				child_pid;
	char				user[MAX_NAME_LEN + 1];
	char				ip[INET_ADDRSTRLEN];
	uint8_t				screen[ROWS][COLS];
	uint8_t				dirty[ROWS];
	uint8_t				cur_y, cur_x;
} Session;

/* ──────────────────────────── PTY helpers ───────────────────────────── */
static int spawn_lynx(const char *home_dir)
{
	struct termios tio;
	tcgetattr(STDIN_FILENO, &tio);
	cfmakeraw(&tio);				/* raw mode for speed */

	int pty_fd;
	pid_t pid = forkpty(&pty_fd, NULL, &tio, NULL);
	if(pid < 0) return -1;

	if(pid == 0){
		/* child – exec lynx */
		if(chdir(home_dir) != 0){     /* warn, then give up */
			perror("uzenet-lynx: chdir");
			_exit(1);
		}

		execlp("lynx",
			   "lynx",
			   "-term=vt100",
			   "-nocolor",
			   "-vikeys",
			   "-nopause",
			   "-anonymous",
			   "about:blank",
			   NULL);
		_exit(1);
	}
	return pty_fd;				/* parent returns master fd */
}

/* ───────────────────────────── Screen diff ───────────────────────────── */
static void flush_dirty_rows(Session *s)
{
	for(uint8_t r = 0; r < ROWS; r++){
		if(!s->dirty[r]) continue;
		s->dirty[r] = 0;

		uint8_t hdr[2] = { PKT_ROW, r };
		if(xwrite(s->sock_fd, hdr, sizeof hdr) < 0) goto io_err;
		if(xwrite(s->sock_fd, s->screen[r], COLS)   < 0) goto io_err;
	}
	return;
io_err:
	; /* ignore, upper layer will close on next failure */
}

/* crude cursor report – can be enhanced with escape parsing */
static void send_cursor(Session *s)
{
	uint8_t pkt[3] = { PKT_CURSOR, s->cur_y, s->cur_x };
	xwrite(s->sock_fd, pkt, sizeof pkt);
}

/* ───────────────────────────── VT100 parser ──────────────────────────── */
/* extremely small subset: printable chars, CR, LF, CUU, CUD, CUB, CUF */
static void vt_process(Session *s, uint8_t c)
{
	switch (c){
	case '\r': s->cur_x = 0; break;
	case '\n':
		if(s->cur_y < ROWS - 1) s->cur_y++;
		break;
	case 0x1B:		/* ignore full ESC sequences for now */
		break;
	default:
		if(c >= 32 && c < 127){
			if(s->cur_x < COLS && s->cur_y < ROWS){
				if(s->screen[s->cur_y][s->cur_x] != c){
					s->screen[s->cur_y][s->cur_x] = c;
					s->dirty[s->cur_y] = 1;
				}
			}
			if(++s->cur_x >= COLS){
				s->cur_x = 0;
				if(s->cur_y < ROWS - 1) s->cur_y++;
			}
		}
		break;
	}
}

/* ───────────────────────────── Client loop ───────────────────────────── */
static void *client_thread(void *arg)
{
	Session *s = arg;

	/* handshake */
	char hs[HANDSHAKE_LEN];
	if(recv(s->sock_fd, hs, HANDSHAKE_LEN, MSG_WAITALL) != HANDSHAKE_LEN ||
		memcmp(hs, HANDSHAKE_STR, HANDSHAKE_LEN)){
		log_msg("[%s] bad handshake", s->ip);
		goto done;
	}

	/* wait for CMD_LOGIN */
	uint8_t cmd;
	if(recv(s->sock_fd, &cmd, 1, MSG_WAITALL) != 1 || cmd != CMD_LOGIN){
		log_msg("[%s] no login", s->ip);
		goto done;
	}
	uint8_t nl;
	if(recv(s->sock_fd, &nl, 1, MSG_WAITALL) != 1 || nl == 0 || nl > MAX_NAME_LEN)
		goto done;
	if(recv(s->sock_fd, s->user, nl, MSG_WAITALL) != nl) goto done;
	s->user[nl] = 0;

	/* prepare per-user dir */
	char home[256];
	snprintf(home, sizeof home, ROOT_DIR "/%s", s->user);
	mkdir(ROOT_DIR, 0755);
	mkdir(home,     0755);

	/* spawn Lynx */
	s->pty_fd = spawn_lynx(home);
	if(s->pty_fd < 0){
		log_msg("[%s] could not spawn lynx", s->ip);
		goto done;
	}
	log_msg("[%s] user '%s' connected (pid %d)", s->ip, s->user, getpid());

	/* main proxy loop */
	fd_set rfds;
	struct timeval tv;
	for(;;){
		FD_ZERO(&rfds);
		FD_SET(s->sock_fd, &rfds);
		FD_SET(s->pty_fd,  &rfds);
		int mx = (s->sock_fd > s->pty_fd ? s->sock_fd : s->pty_fd) + 1;
		tv.tv_sec = 0; tv.tv_usec = 20000;	/* 20 ms */
		int n = select(mx, &rfds, NULL, NULL, &tv);
		if(n < 0 && errno != EINTR) break;

		/* ─ socket → lynx (keypresses) */
		if(FD_ISSET(s->sock_fd, &rfds)){
			uint8_t hdr[2];
			if(recv(s->sock_fd, hdr, 2, MSG_WAITALL) != 2) goto done;
			if(hdr[0] == CMD_KEY){
				uint8_t k = hdr[1];
				if(xwrite(s->pty_fd, &k, 1) < 0) goto done;
			}
		}

		/* ─ lynx → socket (screen) */
		if(FD_ISSET(s->pty_fd, &rfds)){
			uint8_t buf[256];
			ssize_t rd = read(s->pty_fd, buf, sizeof buf);
			if(rd <= 0) goto done;
			for(ssize_t i = 0; i < rd; i++) vt_process(s, buf[i]);
		}

		/* periodic flush (also after timeout) */
		flush_dirty_rows(s);
		send_cursor(s);
	}

done:
	close(s->sock_fd);
	if(s->pty_fd > 0) close(s->pty_fd);
	free(s);
	return NULL;
}

/* ────────────────────────────── Listener ─────────────────────────────── */
int main(int argc, char **argv)
{
	int port = (argc > 1) ? atoi(argv[1]) : LISTEN_PORT;

	int srv = socket(AF_INET, SOCK_STREAM, 0);
	int yes = 1;
	setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

	struct sockaddr_in a = {
		.sin_family = AF_INET,
		.sin_port   = htons(port),
		.sin_addr   = { .s_addr = INADDR_ANY }
	};
	if(bind(srv, (struct sockaddr *)&a, sizeof a) < 0 ||
		listen(srv, BACKLOG) < 0){
		perror("listen");
		return 1;
	}
	log_msg("uzenet-lynx-server listening on %d", port);

	for(;;){
		struct sockaddr_in ca; socklen_t cl = sizeof ca;
		int cfd = accept(srv, (struct sockaddr *)&ca, &cl);
		if(cfd < 0) continue;

		Session *s = calloc(1, sizeof *s);
		s->sock_fd = cfd;
		inet_ntop(AF_INET, &ca.sin_addr, s->ip, sizeof s->ip);

		pthread_t tid;
		pthread_create(&tid, NULL, client_thread, s);
		pthread_detach(tid);
	}
	return 0;
}
