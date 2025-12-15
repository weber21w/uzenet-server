#include "uzenet-tunnel.h"
#include <unistd.h>
#include <errno.h>
#include <string.h>

int utun_read_full(int fd, void *buf, size_t len){
	uint8_t *p = (uint8_t*)buf;

	while(len){
		ssize_t r = read(fd, p, len);
		if(r == 0) return 0;		/* EOF */
		if(r < 0){
			if(errno == EINTR) continue;
			return -1;
		}
		p   += r;
		len -= (size_t)r;
	}
	return 1;
}

int utun_write_full(int fd, const void *buf, size_t len){
	const uint8_t *p = (const uint8_t*)buf;

	while(len){
		ssize_t w = write(fd, p, len);
		if(w < 0){
			if(errno == EINTR) continue;
			return -1;
		}
		p   += w;
		len -= (size_t)w;
	}
	return 0;
}

int utun_read_frame(int fd, TunnelFrame *fr){
	uint8_t hdr[4];
	int r;

	if(!fr) return -1;

	r = utun_read_full(fd, hdr, sizeof(hdr));
	if(r <= 0) return r;		/* 0 = EOF, -1 = error */

	fr->type   = hdr[0];
	fr->flags  = hdr[1];
	fr->length = (uint16_t)((hdr[2] << 8) | hdr[3]);

	if(fr->length > UTUN_MAX_PAYLOAD){
		/* drain junk and fail */
		uint16_t left = fr->length;
		uint8_t tmp[128];
		while(left){
			size_t chunk = (left > sizeof(tmp)) ? sizeof(tmp) : left;
			if(utun_read_full(fd, tmp, chunk) <= 0)
				break;
			left -= (uint16_t)chunk;
		}
		return -1;
	}

	if(fr->length){
		if(utun_read_full(fd, fr->data, fr->length) <= 0)
			return -1;
	}
	return 1;
}

int utun_write_frame(int fd, const TunnelFrame *fr){
	uint8_t hdr[4];
	uint16_t len;

	if(!fr) return -1;

	len = fr->length;
	if(len > UTUN_MAX_PAYLOAD) len = UTUN_MAX_PAYLOAD;

	hdr[0] = fr->type;
	hdr[1] = fr->flags;
	hdr[2] = (uint8_t)(len >> 8);
	hdr[3] = (uint8_t)(len & 0xff);

	if(utun_write_full(fd, hdr, sizeof(hdr)) < 0) return -1;
	if(len && utun_write_full(fd, fr->data, len) < 0) return -1;

	return 0;
}
