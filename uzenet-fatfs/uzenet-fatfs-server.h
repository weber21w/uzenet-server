#ifndef UZENET_FATFS_SERVER_H
#define UZENET_FATFS_SERVER_H

#include <stdio.h>          // for FILE*
#include <stdint.h>
#include <pthread.h>
#include <netinet/in.h>     // for INET_ADDRSTRLEN

#define BACKLOG	32
#define HANDSHAKE_TIMEOUT_SECS	4

#define MAX_NAME_LEN 255
#define MAX_PATH_LEN 512
#define MAX_READ_SIZE 512
#define PASSWORD_LEN 12

#define GUEST_DIR      "uzenetfs-guest"
#define USER_PREFIX    "uzenetfs-"
#define HANDSHAKE_STRING "UFS-HANDSHAKE-READY"

#define USER_QUOTA_BYTES      (8ULL * 1024 * 1024 * 1024) // 8 GiB
#define USER_FILE_LIMIT       65535    // hard cap on files
#define USER_FILE_WARN_THRESHOLD 20000 // warn when reached

// Protocol command codes
enum {
	CMD_MOUNT      = 0x01,
	CMD_READDIR    = 0x02,
	CMD_OPEN       = 0x03,
	CMD_READ       = 0x04,
	CMD_LSEEK      = 0x05,
	CMD_CLOSE      = 0x06,
	CMD_OPTS       = 0x07,
	CMD_LOGIN      = 0x08,
	CMD_WRITE      = 0x09,
	CMD_CREATE     = 0x0A,
	CMD_GETOPT     = 0x0B,
	CMD_HASHINDEX  = 0x0C,
	CMD_STAT       = 0x0D,
	CMD_DELETE     = 0x0E,
	CMD_TIME       = 0x0F,
	CMD_RENAME     = 0x10,
	CMD_MKDIR      = 0x11,
	CMD_RMDIR      = 0x12,
	CMD_LABEL      = 0x13,
	CMD_FREESPACE  = 0x14,
	CMD_TRUNCATE   = 0x15
};

// Per-client state
typedef struct {
	int        fd;                                // socket fd
	char       client_ip[INET_ADDRSTRLEN];        // client address
	FILE      *open_file;                         // current FILE*
	char       mount_root[MAX_PATH_LEN];          // current root path
	uint32_t   current_offset;                    // for LSEEK
	int        is_guest;                          // guest vs. logged-in
	int        enable_lfn, enable_crc, enable_hash; // options
	char       user_id[PASSWORD_LEN+1];           // username/password
} ClientContext;

// Per-user quota tracking
struct user_quota {
	char           username[MAX_NAME_LEN]; // user_id
	char           base_path[MAX_PATH_LEN]; 
	uint64_t       usage_bytes;
	uint32_t       file_count;
	int            ready;
	pthread_mutex_t lock;
};

#define MAX_USERS 64
extern struct user_quota user_quotas[MAX_USERS];

// Public server API
void     quota_init(const char *user, const char *path);
int      quota_check(const char *user, uint64_t new_bytes, int check_files);
int      start_uzenet_fatfs_server(int port);

#endif // UZENET_FATFS_SERVER_H
