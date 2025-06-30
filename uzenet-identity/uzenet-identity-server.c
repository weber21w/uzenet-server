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
#include <pthread.h>
#include <sys/stat.h>
#include <syslog.h>
#include <openssl/sha.h>
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
static pthread_mutex_t db_lock = PTHREAD_MUTEX_INITIALIZER;

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

	pthread_mutex_lock(&db_lock);
	HASH_CLEAR(hh_name, users_by_name);
	HASH_CLEAR(hh_id, users_by_id);
	users_by_name = tmp_by_name;
	users_by_id = tmp_by_id;
	pthread_mutex_unlock(&db_lock);
}

void uzenet_identity_init(void){
	struct stat st;
	if(stat(DB_PATH, &st) == 0)
		db_mtime = st.st_mtime;
	load_users();
	unlink(SOCK_PATH);
}

static int recv_line(int fd, char *buf, size_t maxlen){
	size_t len = 0;
	while(len < maxlen - 1){
		char c;
		int r = recv(fd, &c, 1, 0);
		if(r <= 0) return -1;
		if(c == '\n') break;
		buf[len++] = c;
	}
	buf[len] = 0;
	return len;
}

int uzenet_identity_check_fd(int fd, struct uzenet_identity *out){
	char line[128];
	if(recv_line(fd, line, sizeof(line)) <= 0)
		return 0;
	trim(line);

	pthread_mutex_lock(&db_lock);
	struct user *u = NULL;

	if(strcmp(line, "000000") == 0){
		// Guest
		out->user_id = 0xFFFF;
		strcpy(out->name13, "000000");
		strcpy(out->name8, "000000");
		strcpy(out->name6, "000000");
		out->flags = 'R';
		pthread_mutex_unlock(&db_lock);
		return 1;
	}

	u = NULL;
	HASH_FIND_STR(users_by_name, line, u);
	if(!u){
		pthread_mutex_unlock(&db_lock);
		return 0;
	}

	send(fd, "password:\n", 10, 0);
	if(recv_line(fd, line, sizeof(line)) <= 0){
		pthread_mutex_unlock(&db_lock);
		return 0;
	}
	char hash[65];
	SHA256((const unsigned char *)line, strlen(line), (unsigned char *)hash);
	for(int i = 0; i < 32; ++i)
		sprintf(hash + i * 2, "%02x", ((unsigned char *)hash)[i]);

	if(strcmp(hash, u->hash) != 0){
		pthread_mutex_unlock(&db_lock);
		return 0;
	}

	out->user_id = u->user_id;
	strcpy(out->name13, u->name13);
	strcpy(out->name8, u->name8);
	strcpy(out->name6, u->name6);
	out->flags = u->flags;

	pthread_mutex_unlock(&db_lock);
	return 1;
}
