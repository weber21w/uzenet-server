#ifndef UZENET_SSH_SERVER_H
#define UZENET_SSH_SERVER_H

#include <stdint.h>
#include <unistd.h>

// Called once at startup
void uzenet_ssh_server_init(void);

// Called for each framed connection from uzenet-room
// Returns 0 if session terminated cleanly
int uzenet_ssh_handle(int client_fd, int user_id);

#endif
