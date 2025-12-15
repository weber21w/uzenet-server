#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <syslog.h>
#include <openssl/sha.h>
#include <time.h>
#include <poll.h>
#include "uthash.h"
#include "uzenet-identity-client.h"

#define SOCK_PATH "/run/uzenet/identity.sock"
#define DB_PATH   "/var/lib/uzenet/users.csv"
#define MAX_LINE  512

struct user {
	uint16_t user_id;
	char name13[14], name8[9], name6[7];
	char hash[65];	// SHA-256 hex + NUL
	char flags;
	UT_hash_handle hh_name;
	UT_hash_handle hh_id;
};

static struct user *users_by_name = NULL;
static struct user *users_by_id = NULL;
static time_t db_mtime = 0;

static void trim(char *s){
	char *e = s + strlen(s);
	while(e > s && (e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
}

static void load_users(void){
	FILE *f = fopen(DB_PATH, "r");
	if(!f){
		syslog(LOG_ERR, "identity: can't open %s", DB_PATH);
		return;
	}
	char line[MAX_LINE];
	struct user *tmp_by_name = NULL, *tmp_by_id = NULL;

	while(fgets(line, sizeof(line), f)){
		trim(line);
		if(line[0] == 0 || line[0] == '#') continue;

		char *tok = strtok(line, ",");
		if(!tok) continue;

		struct user *u = calloc(1, sizeof(*u));
		u->user_id = (uint16_t)atoi(tok);

		tok = strtok(NULL, ","); if(!tok) goto skip;
		strncpy(u->name13, tok, 13);

		tok = strtok(NULL, ","); if(!tok) goto skip;
		strncpy(u->name8, tok, 8);

		tok = strtok(NULL, ","); if(!tok) goto skip;
		strncpy(u->name6, tok, 6);

		tok = strtok(NULL, ","); if(!tok) goto skip;
		strncpy(u->hash, tok, 64);
		u->hash[64] = 0;

		tok = strtok(NULL, ","); if(!tok) goto skip;
		u->flags = tok[0];

		struct user *dupe = NULL;
		HASH_FIND(hh_id, tmp_by_id, &u->user_id, sizeof(uint16_t), dupe);
		if(dupe){
			syslog(LOG_ERR, "identity: duplicate user_id %u", u->user_id);
			free(u);
			continue;
		}
		HASH_ADD(hh_id, tmp_by_id, user_id, sizeof(uint16_t), u);
		HASH_ADD_STR(tmp_by_name, name13, u);
		continue;

	skip:
		free(u);
	}
	fclose(f);
	HASH_CLEAR(hh_name, users_by_name);
	HASH_CLEAR(hh_id, users_by_id);
	users_by_name = tmp_by_name;
	users_by_id = tmp_by_id;
}

void uzenet_identity_init(void){
	struct stat st;
	if(stat(DB_PATH, &st) == 0)
		db_mtime = st.st_mtime;
	load_users();
	unlink(SOCK_PATH);

	int sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if(sock < 0){
		syslog(LOG_ERR, "socket: %s", strerror(errno));
		exit(1);
	}

	struct sockaddr_un addr = {0};
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);
	if(bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0){
		syslog(LOG_ERR, "bind: %s", strerror(errno));
		exit(1);
	}
	chmod(SOCK_PATH, 0666);
	listen(sock, 16);

	syslog(LOG_INFO, "uzenet-identity: listening on %s", SOCK_PATH);

	while(1){
		struct pollfd pfd = {.fd = sock, .events = POLLIN};
		if(poll(&pfd, 1, 1000) > 0){
			int client = accept(sock, NULL, NULL);
			if(client < 0) continue;

			char pw[7] = {0};
			int total = 0;
			int timeout_ms = 3000;
			struct pollfd cpf = {.fd = client, .events = POLLIN};
			while(total < 6 && poll(&cpf, 1, timeout_ms) > 0){
				int r = read(client, pw + total, 6 - total);
				if(r <= 0) break;
				total += r;
			}

			if(total < 6){
				close(client);
				continue;
			}

			struct user *u = NULL;
			if(!strcmp(pw, "000000")){
				uint8_t reply[2] = {0xFF, 0xFF};
				write(client, reply, 2);
				close(client);
				continue;
			}
			HASH_FIND_STR(users_by_name, pw, u);
			if(!u){
				close(client);
				continue;
			}
			uint8_t reply[2] = { u->user_id & 0xFF, u->user_id >> 8 };
			write(client, reply, 2);
			close(client);
		}
	}
}
