#ifndef UZENET_TUNNEL_H
#define UZENET_TUNNEL_H

#include <stdint.h>
#include <stddef.h>

#define UTUN_MAX_PAYLOAD	256	/* or 64 if you want lichess-sized frames */

#define UTUN_TYPE_LOGIN		0x01
#define UTUN_TYPE_DATA		0x02
#define UTUN_TYPE_PING		0x03
#define UTUN_TYPE_PONG		0x04

typedef struct{
	uint8_t		type;
	uint8_t		flags;
	uint16_t	length;		/* host-endian */
	uint8_t		data[UTUN_MAX_PAYLOAD];
} TunnelFrame;

/* blocking helpers that either complete or fail; hide partial reads/writes */
int utun_read_full(int fd, void *buf, size_t len);
int utun_write_full(int fd, const void *buf, size_t len);

/* frame helpers: return:
 *   >0  success (1)
 *    0  clean EOF
 *   <0  error
 */
int utun_read_frame(int fd, TunnelFrame *fr);
int utun_write_frame(int fd, const TunnelFrame *fr);

#endif
