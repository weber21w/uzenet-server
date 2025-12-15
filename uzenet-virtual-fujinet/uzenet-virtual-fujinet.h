#ifndef UZENET_VIRTUAL_FUJINET_H
#define UZENET_VIRTUAL_FUJINET_H

#include <stdint.h>
#include "../uzenet-tunnel/uzenet-tunnel.h"

#define VFN_SOCKET_PATH "/run/uzenet/virtual-fujinet.sock"

/* Per-client context for uzenet-virtual-fujinet */
typedef struct{
	int			fd;
	uint16_t	user_id;

	/* TODO: add per-user prefs, TNFS sessions, HTTPS state, etc. */
} vfn_client_t;

/* Entry point for a single accepted AF_UNIX connection. */
void uzenet_virtual_fujinet_handle(int fd);

#endif /* UZENET_VIRTUAL_FUJINET_H */
