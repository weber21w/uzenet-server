#ifndef UZENET_IDENTITY_CLIENT_H
#define UZENET_IDENTITY_CLIENT_H

#include <stdint.h>

struct uzenet_identity {
	uint16_t user_id;		// new: persistent per-user
	char name13[14];
	char name8[9];
	char name6[7];
	char flags;				// 'R', 'G', 'A', etc.
};

// Called on an accepted fd, returns 1 on success
int uzenet_identity_check_fd(int fd, struct uzenet_identity *out);
void uzenet_identity_init(void);

#endif
