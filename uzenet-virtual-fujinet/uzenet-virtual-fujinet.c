#define _POSIX_C_SOURCE 200809L
#include "uzenet-virtual-fujinet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

/*
 * Protocol layering:
 *
 *   Uzebox <-> uzenet-room  : 0xF0|tunnel_id + len + payload
 *   uzenet-room <-> this    : uzenet-tunnel (TunnelFrame)
 *   payload (this service)  : virtual FujiNet commands
 *
 * This file only cares about the last layer (TunnelFrame + service payload).
 */

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static int vfn_send_data(vfn_client_t *c, const void *buf, uint16_t len){
	TunnelFrame fr;

	if(len > UTUN_MAX_PAYLOAD)
		len = UTUN_MAX_PAYLOAD;

	memset(&fr, 0, sizeof(fr));
	fr.type   = UTUN_TYPE_DATA;
	fr.flags  = 0;
	fr.length = len;
	if(len)
		memcpy(fr.data, buf, len);

	return utun_write_frame(c->fd, &fr);
}

/* For now, we treat the first byte of DATA payload as a "command id". */
enum{
	VFN_CMD_NOP			= 0x00,
	VFN_CMD_RESET		= 0x01,
	VFN_CMD_TNFS_OPEN	= 0x10,
	VFN_CMD_TNFS_READ	= 0x11,
	VFN_CMD_TNFS_CLOSE	= 0x12,
	VFN_CMD_HTTP_GET	= 0x20,
	VFN_CMD_HTTP_HEAD	= 0x21,
	/* ...extend as needed... */
};

/* Simple error reply format (service-level, inside DATA):
 *   [0] = 0xFF (error marker)
 *   [1] = error code
 *   [2] = reserved / extra
 *   [3] = reserved / extra
 */
static void vfn_send_error(vfn_client_t *c, uint8_t code, uint8_t a0, uint8_t a1){
	uint8_t msg[4];

	msg[0] = 0xFF;
	msg[1] = code;
	msg[2] = a0;
	msg[3] = a1;
	(void)vfn_send_data(c, msg, 4);
}

/* ------------------------------------------------------------------------- */
/* Command handlers (stubs for now)                                         */
/* ------------------------------------------------------------------------- */

static void vfn_handle_cmd_reset(vfn_client_t *c, const uint8_t *data, uint16_t len){
	(void)data;
	(void)len;

	/* TODO: Clear per-user TNFS sessions, HTTP state, cached handles, etc. */
	fprintf(stderr, "[virtual-fujinet] user %u: RESET\n", (unsigned)c->user_id);
}

static void vfn_handle_cmd_tnfs_open(vfn_client_t *c, const uint8_t *data, uint16_t len){
	(void)c;
	(void)data;
	(void)len;

	/* TODO: Parse TNFS path, mode, etc. and open via libtnfs or custom code.
	 * For now we just stub an error.
	 */
	fprintf(stderr, "[virtual-fujinet] user %u: TNFS_OPEN (stub)\n", (unsigned)c->user_id);
	vfn_send_error(c, 1, VFN_CMD_TNFS_OPEN, 0);
}

static void vfn_handle_cmd_tnfs_read(vfn_client_t *c, const uint8_t *data, uint16_t len){
	(void)c;
	(void)data;
	(void)len;

	/* TODO: Read from TNFS handle, send back up to N bytes per frame. */
	fprintf(stderr, "[virtual-fujinet] user %u: TNFS_READ (stub)\n", (unsigned)c->user_id);
	vfn_send_error(c, 1, VFN_CMD_TNFS_READ, 0);
}

static void vfn_handle_cmd_tnfs_close(vfn_client_t *c, const uint8_t *data, uint16_t len){
	(void)c;
	(void)data;
	(void)len;

	/* TODO: Close TNFS handle. */
	fprintf(stderr, "[virtual-fujinet] user %u: TNFS_CLOSE (stub)\n", (unsigned)c->user_id);
	vfn_send_error(c, 1, VFN_CMD_TNFS_CLOSE, 0);
}

static void vfn_handle_cmd_http_get(vfn_client_t *c, const uint8_t *data, uint16_t len){
	(void)c;
	(void)data;
	(void)len;

	/* TODO: Use OpenSSL/HTTP client to fetch URL, stream back to client.
	 * URL can be in the payload as a NUL-terminated string.
	 */
	fprintf(stderr, "[virtual-fujinet] user %u: HTTP_GET (stub)\n", (unsigned)c->user_id);
	vfn_send_error(c, 2, VFN_CMD_HTTP_GET, 0);
}

static void vfn_handle_cmd_http_head(vfn_client_t *c, const uint8_t *data, uint16_t len){
	(void)c;
	(void)data;
	(void)len;

	fprintf(stderr, "[virtual-fujinet] user %u: HTTP_HEAD (stub)\n", (unsigned)c->user_id);
	vfn_send_error(c, 2, VFN_CMD_HTTP_HEAD, 0);
}

/* Main DATA dispatcher: first byte = command id, rest = arguments */
static void vfn_handle_data(vfn_client_t *c, const uint8_t *data, uint16_t len){
	uint8_t cmd;

	if(len == 0){
		return;
	}

	cmd = data[0];
	data++;
	len--;

	switch(cmd){
	case VFN_CMD_NOP:
		/* Keep-alive / ping from client. No-op for now. */
		break;

	case VFN_CMD_RESET:
		vfn_handle_cmd_reset(c, data, len);
		break;

	case VFN_CMD_TNFS_OPEN:
		vfn_handle_cmd_tnfs_open(c, data, len);
		break;

	case VFN_CMD_TNFS_READ:
		vfn_handle_cmd_tnfs_read(c, data, len);
		break;

	case VFN_CMD_TNFS_CLOSE:
		vfn_handle_cmd_tnfs_close(c, data, len);
		break;

	case VFN_CMD_HTTP_GET:
		vfn_handle_cmd_http_get(c, data, len);
		break;

	case VFN_CMD_HTTP_HEAD:
		vfn_handle_cmd_http_head(c, data, len);
		break;

	default:
		fprintf(stderr,
			"[virtual-fujinet] user %u: unknown cmd 0x%02X (len=%u)\n",
			(unsigned)c->user_id, cmd, (unsigned)len);
		vfn_send_error(c, 0xFF, cmd, 0);
		break;
	}
}

/* ------------------------------------------------------------------------- */
/* Single-connection handler                                                 */
/* ------------------------------------------------------------------------- */

void uzenet_virtual_fujinet_handle(int fd){
	vfn_client_t c;
	TunnelFrame fr;
	int rc;

	memset(&c, 0, sizeof(c));
	c.fd      = fd;
	c.user_id = 0xFFFF;

	/* 1) Expect a LOGIN frame from uzenet-room. */
	rc = utun_read_frame(fd, &fr);
	if(rc <= 0){
		/* EOF or error before LOGIN. */
		close(fd);
		return;
	}

	if(fr.type == UTUN_TYPE_LOGIN && fr.length >= 2){
		uint16_t uid = (uint16_t)((fr.data[0] << 8) | fr.data[1]);
		c.user_id = uid;
		fprintf(stderr,
			"[virtual-fujinet] LOGIN user_id=%u\n",
			(unsigned)c.user_id);
	}else{
		/* Unexpected first frame, drop client. */
		fprintf(stderr,
			"[virtual-fujinet] expected LOGIN frame, got type=0x%02X\n",
			fr.type);
		close(fd);
		return;
	}

	/* 2) Main frame loop. */
	for(;;){
		rc = utun_read_frame(fd, &fr);
		if(rc == 0){
			/* EOF */
			fprintf(stderr,
				"[virtual-fujinet] user %u: disconnect\n",
				(unsigned)c.user_id);
			break;
		}
		if(rc < 0){
			/* I/O or framing error */
			fprintf(stderr,
				"[virtual-fujinet] user %u: read error (%s)\n",
				(unsigned)c.user_id, strerror(errno));
			break;
		}

		if(fr.type == UTUN_TYPE_DATA){
			vfn_handle_data(&c, fr.data, fr.length);
		}else if(fr.type == UTUN_TYPE_PING){
			TunnelFrame pong;
			memset(&pong, 0, sizeof(pong));
			pong.type   = UTUN_TYPE_PONG;
			pong.flags  = 0;
			pong.length = 0;
			(void)utun_write_frame(c.fd, &pong);
		}else{
			/* Ignore other types for now. */
		}
	}

	close(fd);
}

/* ------------------------------------------------------------------------- */
/* Listener / main                                                           */
/* ------------------------------------------------------------------------- */

static int listen_fd = -1;
static volatile sig_atomic_t quitting = 0;

static void on_sigint(int sig){
	(void)sig;
	quitting = 1;
}

static void *client_thread_main(void *arg){
	int fd = *(int*)arg;
	free(arg);
	uzenet_virtual_fujinet_handle(fd);
	return NULL;
}

int main(int argc, char **argv){
	int fd;
	struct sockaddr_un addr;

	(void)argc;
	(void)argv;

	signal(SIGINT, on_sigint);
	signal(SIGTERM, on_sigint);

	unlink(VFN_SOCKET_PATH);

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if(fd < 0){
		perror("socket");
		return 1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, VFN_SOCKET_PATH, sizeof(addr.sun_path) - 1);

	if(bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
		perror("bind");
		close(fd);
		return 1;
	}

	if(listen(fd, 16) < 0){
		perror("listen");
		close(fd);
		return 1;
	}

	listen_fd = fd;
	fprintf(stderr, "[virtual-fujinet] listening on %s\n", VFN_SOCKET_PATH);

	while(!quitting){
		int cfd = accept(listen_fd, NULL, NULL);
		if(cfd < 0){
			if(errno == EINTR)
				continue;
			perror("accept");
			break;
		}

		int *pfd = malloc(sizeof(int));
		if(!pfd){
			close(cfd);
			continue;
		}
		*pfd = cfd;

		pthread_t tid;
		if(pthread_create(&tid, NULL, client_thread_main, pfd) != 0){
			perror("pthread_create");
			close(cfd);
			free(pfd);
			continue;
		}
		pthread_detach(tid);
	}

	close(listen_fd);
	unlink(VFN_SOCKET_PATH);
	return 0;
}
