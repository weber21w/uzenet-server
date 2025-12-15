#include "uzenet-fatfs-server.h"
#include "uzenet-tunnel.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <time.h>
#include <limits.h>

// -----------------------------------------------------------------------------
// Config
// -----------------------------------------------------------------------------

#define FATFS_SOCKET_PATH "/run/uzenet/fatfs.sock"

// For LOGIN meta frames (same as used in uzenet-lichess / virtual-fujinet)
typedef struct{
	uint16_t user_id;
	uint16_t reserved;
} TunnelLoginMeta;

// Simple byte-stream adapter over TunnelFrame DATA frames
typedef struct{
	int      fd;
	uint8_t  buf[1024];
	size_t   len;
} TunnelStream;

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

#pragma GCC diagnostic ignored "-Wformat-truncation"

// Define the quota array (extern in the header)
struct user_quota user_quotas[MAX_USERS];

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

// Safe join of base + sub into out, forbidding escapes above base
static int safe_path(const char *base, const char *sub, char *out){
	char tmp[MAX_PATH_LEN];
	snprintf(tmp, sizeof(tmp), "%s/%s", base, sub);
	char resolved[MAX_PATH_LEN];
	if(!realpath(tmp, resolved)) return 0;
	size_t bl = strlen(base);
	if(strncmp(resolved, base, bl) != 0) return 0;
	// Ensure either exact match or a slash follows
	if(resolved[bl] != '/' && resolved[bl] != '\0') return 0;
	strncpy(out, resolved, MAX_PATH_LEN - 1);
	out[MAX_PATH_LEN - 1] = '\0';
	return 1;
}

// CRC-16/XMODEM implementation
static uint16_t crc16_xmodem(const uint8_t *data, size_t len){
	uint16_t crc = 0x0000;
	for(size_t i = 0; i < len; i++){
		crc ^= (uint16_t)data[i] << 8;
		for(int b = 0; b < 8; b++){
			if(crc & 0x8000) crc = (crc << 1) ^ 0x1021;
			else            crc <<= 1;
		}
	}
	return crc;
}

// Log via syslog with epoch timestamp
static void log_msg(const char *fmt, ...){
	va_list ap;
	va_start(ap, fmt);
	openlog("uzenet_fatfs", LOG_PID, LOG_LOCAL6);
	vsyslog(LOG_INFO, fmt, ap);
	closelog();
	va_end(ap);
}

// -----------------------------------------------------------------------------
// Quota tracking
// -----------------------------------------------------------------------------

static void *quota_worker(void *arg){
	struct user_quota *uq = arg;
	while(1){
		DIR *d = opendir(uq->base_path);
		uint64_t total = 0;
		uint32_t files = 0;
		if(d){
			struct dirent *e;
			while((e = readdir(d))){
				if(e->d_name[0] == '.') continue;
				char path[MAX_PATH_LEN];

				snprintf(path, sizeof(path), "%s/%.*s",
					uq->base_path,
					(int)(sizeof(path) - strlen(uq->base_path) - 2),
					e->d_name);
				struct stat st;
				if(stat(path, &st) == 0 && S_ISREG(st.st_mode)){
					total += st.st_size;
					files++;
				}
			}
			closedir(d);
		}
		pthread_mutex_lock(&uq->lock);
		uq->usage_bytes = total;
		uq->file_count  = files;
		uq->ready       = 1;
		pthread_mutex_unlock(&uq->lock);
		if(files > USER_FILE_LIMIT){
			syslog(LOG_WARNING, "[QUOTA] '%s' exceeds file limit: %u",
			       uq->username, files);
		}else if(files > USER_FILE_WARN_THRESHOLD){
			syslog(LOG_NOTICE, "[QUOTA] '%s' high file count: %u",
			       uq->username, files);
		}
		sleep(60);
	}
	return NULL;
}

void quota_init(const char *user, const char *path){
	for(int i = 0; i < MAX_USERS; i++){
		struct user_quota *uq = &user_quotas[i];
		if(uq->username[0] == '\0' || strcmp(uq->username, user) == 0){
			strncpy(uq->username, user, MAX_NAME_LEN);
			strncpy(uq->base_path, path, MAX_PATH_LEN);
			pthread_mutex_init(&uq->lock, NULL);
			uq->ready       = 0;
			uq->usage_bytes = 0;
			uq->file_count  = 0;
			// initial synchronous scan
			DIR *d = opendir(path);
			if(d){
				struct dirent *e;
				uint64_t total = 0;
				uint32_t files = 0;
				while((e = readdir(d))){
					if(e->d_name[0] == '.') continue;
					char p[MAX_PATH_LEN];
					snprintf(p, sizeof(p), "%s/%s", path, e->d_name);
					struct stat st;
					if(stat(p, &st) == 0 && S_ISREG(st.st_mode)){
						total += st.st_size;
						files++;
					}
				}
				closedir(d);
				pthread_mutex_lock(&uq->lock);
				uq->usage_bytes = total;
				uq->file_count  = files;
				uq->ready       = 1;
				pthread_mutex_unlock(&uq->lock);
			}
			// background updates
			pthread_t tid;
			pthread_create(&tid, NULL, quota_worker, uq);
			pthread_detach(tid);
			break;
		}
	}
}

int quota_check(const char *user, uint64_t new_bytes, int check_files){
	for(int i = 0; i < MAX_USERS; i++){
		struct user_quota *uq = &user_quotas[i];
		if(strcmp(uq->username, user) == 0){
			pthread_mutex_lock(&uq->lock);
			int      ready = uq->ready;
			uint64_t used  = uq->usage_bytes;
			uint32_t files = uq->file_count;
			pthread_mutex_unlock(&uq->lock);
			if(!ready) return -3;
			if(used + new_bytes > USER_QUOTA_BYTES){
				log_msg("'%s' over quota: %llu + %llu > %llu",
				        user,
				        (unsigned long long)used,
				        (unsigned long long)new_bytes,
				        (unsigned long long)USER_QUOTA_BYTES);
				return -1;
			}
			if(check_files && files >= USER_FILE_LIMIT){
				log_msg("'%s' hit file limit: %u", user, files);
				return -2;
			}
			return 0;
		}
	}
	return 0;
}

// -----------------------------------------------------------------------------
// Tunnel byte-stream helpers
// -----------------------------------------------------------------------------

// Fill TunnelStream.buf with one DATA frame's payload
static int ts_fill(TunnelStream *ts){
	TunnelFrame fr;
	int r = ReadTunnelFramed(ts->fd, &fr);
	if(r <= 0){
		return r; // 0 = EOF, <0 = error
	}
	if(fr.type == TUNNEL_TYPE_LOGIN){
		// Unexpected extra LOGIN; just ignore.
		return 1;
	}
	if(fr.type != TUNNEL_TYPE_DATA || fr.length == 0){
		// Unknown / empty; ignore.
		return 1;
	}
	if(ts->len + fr.length > sizeof(ts->buf)){
		// Overflow: treat as error.
		return -1;
	}
	memcpy(ts->buf + ts->len, fr.data, fr.length);
	ts->len += fr.length;
	return 1;
}

// Read exactly 'need' bytes into out (like MSG_WAITALL semantics)
static int ts_read_exact(TunnelStream *ts, void *out, size_t need){
	uint8_t *dst = (uint8_t*)out;
	size_t got = 0;

	while(got < need){
		if(ts->len == 0){
			int r = ts_fill(ts);
			if(r <= 0){
				return -1; // EOF or error
			}
			continue;
		}
		size_t take = need - got;
		if(take > ts->len) take = ts->len;
		memcpy(dst + got, ts->buf, take);
		memmove(ts->buf, ts->buf + take, ts->len - take);
		ts->len -= take;
		got += take;
	}
	return 0;
}

// Write arbitrary bytes as one or more DATA frames
static int ts_write(TunnelStream *ts, const void *buf, size_t len){
	const uint8_t *p = (const uint8_t*)buf;
	while(len > 0){
		size_t chunk = len;
		if(chunk > TUNNEL_MAX_PAYLOAD) chunk = TUNNEL_MAX_PAYLOAD;

		TunnelFrame fr;
		fr.type   = TUNNEL_TYPE_DATA;
		fr.flags  = 0;
		fr.length = (uint16_t)chunk;
		memcpy(fr.data, p, chunk);

		if(WriteTunnelFramed(ts->fd, &fr) < 0){
			return -1;
		}
		p   += chunk;
		len -= chunk;
	}
	return 0;
}

static int ts_write_u8(TunnelStream *ts, uint8_t v){
	return ts_write(ts, &v, 1);
}

// -----------------------------------------------------------------------------
// Thread argument
// -----------------------------------------------------------------------------

typedef struct {
	int client_fd;
} ThreadArg;

// -----------------------------------------------------------------------------
// Client handler
// -----------------------------------------------------------------------------

static void *handle_client(void *arg){
	ThreadArg *ta = (ThreadArg*)arg;
	ClientContext ctx;
	TunnelStream ts;
	TunnelFrame  first_fr;

	memset(&ctx, 0, sizeof(ctx));
	memset(&ts,  0, sizeof(ts));

	ctx.fd = ta->client_fd;
	// We no longer have a direct remote IP (we're behind uzenet-room).
	strncpy(ctx.client_ip, "uzenet-room", sizeof(ctx.client_ip) - 1);
	ctx.client_ip[sizeof(ctx.client_ip) - 1] = '\0';

	strncpy(ctx.mount_root, GUEST_DIR, MAX_PATH_LEN);
	strncpy(ctx.user_id,    "guest",   PASSWORD_LEN);
	ctx.is_guest = 1;

	ts.fd  = ctx.fd;
	ts.len = 0;

	free(ta);

	// Expect an initial LOGIN frame from uzenet-room
	int r = ReadTunnelFramed(ctx.fd, &first_fr);
	if(r <= 0){
		close(ctx.fd);
		return NULL;
	}

	if(first_fr.type == TUNNEL_TYPE_LOGIN &&
	   first_fr.length >= sizeof(TunnelLoginMeta)){
		const TunnelLoginMeta *meta = (const TunnelLoginMeta*)first_fr.data;
		uint16_t uid = meta->user_id;

		if(uid != 0xFFFF){
			snprintf(ctx.user_id, PASSWORD_LEN, "%u", (unsigned)uid);
			ctx.is_guest = 0;
		}
		// For now we still keep mount_root = GUEST_DIR.
		// You can later map uid -> per-user directory here.
		log_msg("[room] LOGIN user_id=%u (guest=%d)",
		        (unsigned)uid, ctx.is_guest ? 1 : 0);
	}else if(first_fr.type == TUNNEL_TYPE_DATA && first_fr.length > 0){
		// No LOGIN (older room?), treat payload as start of byte stream.
		if(first_fr.length > sizeof(ts.buf)){
			first_fr.length = sizeof(ts.buf);
		}
		memcpy(ts.buf, first_fr.data, first_fr.length);
		ts.len = first_fr.length;
	}else{
		// Unknown; just continue with empty stream buffer.
	}

	// Optional: initialize quota system for this user/root
	// (Currently quotas are effectively disabled unless quota_init is called.
	//  If you want per-user quotas, uncomment this line and make sure
	//  mount_root is user-specific.)
	// quota_init(ctx.user_id, ctx.mount_root);

	// Handshake over tunnel (same HANDSHAKE_STRING as before)
	{
		char hb[sizeof(HANDSHAKE_STRING)];
		if(ts_read_exact(&ts, hb, sizeof(hb)) != 0 ||
		   memcmp(hb, HANDSHAKE_STRING, sizeof(hb)) != 0){
			log_msg("[%s] Invalid handshake", ctx.client_ip);
			close(ctx.fd);
			return NULL;
		}
	}

	// Command loop (unchanged protocol, now using ts_read_exact / ts_write)
	for(;;){
		uint8_t cmd;
		if(ts_read_exact(&ts, &cmd, 1) != 0) break;

		switch(cmd){
			case CMD_MOUNT: {
				uint8_t len;
				if(ts_read_exact(&ts, &len, 1) != 0) goto done;

				char rel[MAX_PATH_LEN + 1];
				if(len > MAX_PATH_LEN) len = MAX_PATH_LEN;
				if(ts_read_exact(&ts, rel, len) != 0) goto done;
				rel[len] = 0;

				char nr[MAX_PATH_LEN];
				if(!safe_path(ctx.mount_root, rel, nr) ||
				   access(nr, R_OK | X_OK) != 0){
					uint8_t res = 0x01;
					ts_write_u8(&ts, res);
					log_msg("[%s] MOUNT fail: %s", ctx.client_ip, rel);
				}else{
					strncpy(ctx.mount_root, nr, MAX_PATH_LEN);
					uint8_t res = 0x00;
					ts_write_u8(&ts, res);
					log_msg("[%s] MOUNT -> %s", ctx.client_ip, nr);
				}
				break;
			}

			case CMD_READDIR: {
				DIR *d = opendir(ctx.mount_root);
				if(!d){
					uint8_t res = 0x01;
					ts_write_u8(&ts, res);
					break;
				}
				struct dirent *e;
				while((e = readdir(d))){
					if(!strcmp(e->d_name,".") || !strcmp(e->d_name,".."))
						continue;
					uint8_t nl = (uint8_t)strlen(e->d_name);
					ts_write_u8(&ts, nl);
					ts_write(&ts, e->d_name, nl);

					char p[MAX_PATH_LEN];
					snprintf(p, sizeof(p), "%s/%s", ctx.mount_root, e->d_name);
					struct stat st;
					stat(p, &st);
					uint32_t sz = (uint32_t)st.st_size;
					uint8_t attr = S_ISDIR(st.st_mode) ? 0x10 : 0x00;

					ts_write(&ts, &sz, sizeof(sz));
					ts_write(&ts, &attr, sizeof(attr));

					if(ctx.enable_hash){
						uint16_t h = crc16_xmodem((uint8_t*)e->d_name, nl);
						ts_write(&ts, &h, sizeof(h));
					}
				}
				closedir(d);
				{
					uint8_t end = 0x00;
					ts_write_u8(&ts, end);
				}
				log_msg("[%s] READDIR on %s", ctx.client_ip, ctx.mount_root);
				break;
			}

			case CMD_OPEN: {
				uint8_t nl;
				if(ts_read_exact(&ts, &nl, 1) != 0) goto done;
				char fn[MAX_NAME_LEN + 1] = {0};
				if(nl > MAX_NAME_LEN) nl = MAX_NAME_LEN;
				if(ts_read_exact(&ts, fn, nl) != 0) goto done;
				fn[nl] = 0;

				char path[MAX_PATH_LEN];
				if(!safe_path(ctx.mount_root, fn, path)){
					uint8_t r = 0x01;
					ts_write_u8(&ts, r);
					break;
				}
				if(ctx.open_file) fclose(ctx.open_file);
				ctx.open_file = fopen(path, ctx.is_guest ? "rb" : "r+b");
				uint8_t r2 = ctx.open_file ? 0x00 : 0x01;
				ts_write_u8(&ts, r2);
				log_msg("[%s] OPEN %s -> %s",
				        ctx.client_ip, fn, r2 ? "FAIL" : "OK");
				break;
			}

			case CMD_READ: {
				uint32_t off;
				uint16_t len16;
				if(ts_read_exact(&ts, &off, sizeof(off)) != 0) goto done;
				if(ts_read_exact(&ts, &len16, sizeof(len16)) != 0) goto done;

				if(!ctx.open_file){
					uint8_t err = 0x02;
					ts_write_u8(&ts, err);
					break;
				}

				if(len16 > MAX_READ_SIZE) len16 = MAX_READ_SIZE;
				fseek(ctx.open_file, off, SEEK_SET);
				uint8_t buf[MAX_READ_SIZE];
				size_t rd = fread(buf, 1, len16, ctx.open_file);

				uint8_t ok = 0x00;
				ts_write_u8(&ts, ok);
				uint16_t rl = (uint16_t)rd;
				ts_write(&ts, &rl, sizeof(rl));
				if(rl){
					ts_write(&ts, buf, rl);
				}
				log_msg("[%s] READ %u@%u", ctx.client_ip, rl, off);
				break;
			}

			case CMD_LSEEK: {
				uint32_t off;
				if(ts_read_exact(&ts, &off, sizeof(off)) != 0) goto done;
				if(!ctx.open_file){
					uint8_t err = 0x02;
					ts_write_u8(&ts, err);
					break;
				}
				ctx.current_offset = off;
				{
					uint8_t ok = 0x00;
					ts_write_u8(&ts, ok);
				}
				break;
			}

			case CMD_CLOSE: {
				if(ctx.open_file){
					fclose(ctx.open_file);
					ctx.open_file = NULL;
				}
				{
					uint8_t ok = 0x00;
					ts_write_u8(&ts, ok);
				}
				break;
			}

			case CMD_OPTS: {
				uint8_t opt;
				uint32_t val;
				if(ts_read_exact(&ts, &opt, 1) != 0) goto done;
				if(ts_read_exact(&ts, &val, 4) != 0) goto done;
				if(opt == 1) ctx.enable_lfn  = val;
				if(opt == 2) ctx.enable_crc  = val;
				if(opt == 3) ctx.enable_hash = val;
				{
					uint8_t ok = 0x00;
					ts_write_u8(&ts, ok);
				}
				break;
			}

			case CMD_GETOPT: {
				uint8_t flags = (ctx.enable_lfn ? 1 : 0)
				              | (ctx.enable_crc ? 2 : 0)
				              | (ctx.enable_hash ? 4 : 0);
				ts_write_u8(&ts, flags);
				break;
			}

			case CMD_HASHINDEX: {
				DIR *d = opendir(ctx.mount_root);
				if(!d){
					uint8_t err = 0x01;
					ts_write_u8(&ts, err);
					break;
				}
				struct dirent *e;
				while((e = readdir(d))){
					if(e->d_type != DT_REG) continue;
					uint8_t nl = (uint8_t)strlen(e->d_name);
					uint16_t h = crc16_xmodem((uint8_t*)e->d_name, nl);
					ts_write_u8(&ts, nl);
					ts_write(&ts, e->d_name, nl);
					ts_write(&ts, &h, sizeof(h));
				}
				closedir(d);
				{
					uint8_t end = 0x00;
					ts_write_u8(&ts, end);
				}
				break;
			}

			case CMD_STAT: {
				uint8_t nl;
				if(ts_read_exact(&ts, &nl, 1) != 0) goto done;
				char fn[MAX_NAME_LEN + 1] = {0};
				if(nl > MAX_NAME_LEN) nl = MAX_NAME_LEN;
				if(ts_read_exact(&ts, fn, nl) != 0) goto done;
				fn[nl] = 0;

				char path[MAX_PATH_LEN];
				if(!safe_path(ctx.mount_root, fn, path)){
					uint8_t err = 0x01;
					ts_write_u8(&ts, err);
					break;
				}
				struct stat st;
				if(stat(path, &st) != 0){
					uint8_t err = 0x01;
					ts_write_u8(&ts, err);
				}else{
					uint8_t ok  = 0x00;
					uint32_t sz = (uint32_t)st.st_size;
					uint8_t attr = S_ISDIR(st.st_mode) ? 0x10 : 0x00;
					ts_write_u8(&ts, ok);
					ts_write(&ts, &sz, sizeof(sz));
					ts_write(&ts, &attr, sizeof(attr));
				}
				break;
			}

			case CMD_TIME: {
				uint8_t ok = 0x00;
				uint32_t now = (uint32_t)time(NULL);
				ts_write_u8(&ts, ok);
				ts_write(&ts, &now, sizeof(now));
				break;
			}

			case CMD_RENAME: {
				uint8_t l1, l2;
				if(ts_read_exact(&ts, &l1, 1) != 0) goto done;
				char o[MAX_NAME_LEN + 1] = {0};
				if(l1 > MAX_NAME_LEN) l1 = MAX_NAME_LEN;
				if(ts_read_exact(&ts, o, l1) != 0) goto done;
				o[l1] = 0;

				if(ts_read_exact(&ts, &l2, 1) != 0) goto done;
				char n[MAX_NAME_LEN + 1] = {0};
				if(l2 > MAX_NAME_LEN) l2 = MAX_NAME_LEN;
				if(ts_read_exact(&ts, n, l2) != 0) goto done;
				n[l2] = 0;

				char p1[MAX_PATH_LEN], p2[MAX_PATH_LEN];
				if(!safe_path(ctx.mount_root, o, p1) ||
				   !safe_path(ctx.mount_root, n, p2)){
					uint8_t err = 0x01;
					ts_write_u8(&ts, err);
					break;
				}
				int rr = rename(p1, p2);
				uint8_t res = rr ? 0x01 : 0x00;
				ts_write_u8(&ts, res);
				break;
			}

			case CMD_CREATE: {
				uint8_t nl;
				if(ts_read_exact(&ts, &nl, 1) != 0) goto done;
				char fn[MAX_NAME_LEN + 1] = {0};
				if(nl > MAX_NAME_LEN) nl = MAX_NAME_LEN;
				if(ts_read_exact(&ts, fn, nl) != 0) goto done;
				fn[nl] = 0;

				char path[MAX_PATH_LEN];
				if(!safe_path(ctx.mount_root, fn, path)){
					uint8_t err = 0x01;
					ts_write_u8(&ts, err);
					break;
				}
				int qc = quota_check(ctx.user_id, 0, 1);
				if(qc == -2){
					uint8_t res = 0xFE;
					ts_write_u8(&ts, res);
					break;
				}
				int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0644);
				uint8_t r3 = (fd >= 0) ? 0x00 : 0xFF;
				if(fd >= 0) close(fd);
				ts_write_u8(&ts, r3);
				break;
			}

			case CMD_WRITE: {
				uint8_t nl;
				if(ts_read_exact(&ts, &nl, 1) != 0) goto done;
				char fn[MAX_NAME_LEN + 1] = {0};
				if(nl > MAX_NAME_LEN) nl = MAX_NAME_LEN;
				if(ts_read_exact(&ts, fn, nl) != 0) goto done;
				fn[nl] = 0;

				uint16_t len16;
				if(ts_read_exact(&ts, &len16, 2) != 0) goto done;
				if(len16 > MAX_READ_SIZE){
					uint8_t res = 0xFD;
					ts_write_u8(&ts, res);
					break;
				}
				uint8_t buf[MAX_READ_SIZE];
				if(ts_read_exact(&ts, buf, len16) != 0) goto done;

				char path[MAX_PATH_LEN];
				if(!safe_path(ctx.mount_root, fn, path)){
					uint8_t err = 0x01;
					ts_write_u8(&ts, err);
					break;
				}
				int qc = quota_check(ctx.user_id, len16, 0);
				if(qc == -1){
					uint8_t res = 0xFC;
					ts_write_u8(&ts, res);
					break;
				}
				int fd = open(path, O_WRONLY | O_APPEND);
				uint8_t r4 = 0xFF;
				if(fd >= 0){
					if(write(fd, buf, len16) == (ssize_t)len16) r4 = 0x00;
					close(fd);
				}
				ts_write_u8(&ts, r4);
				break;
			}

			case CMD_DELETE: {
				uint8_t nl;
				if(ts_read_exact(&ts, &nl, 1) != 0) goto done;
				char fn[MAX_NAME_LEN + 1] = {0};
				if(nl > MAX_NAME_LEN) nl = MAX_NAME_LEN;
				if(ts_read_exact(&ts, fn, nl) != 0) goto done;
				fn[nl] = 0;

				char path[MAX_PATH_LEN];
				if(!safe_path(ctx.mount_root, fn, path)){
					uint8_t err = 0x01;
					ts_write_u8(&ts, err);
					break;
				}
				int rr = remove(path);
				uint8_t res = rr ? 0x01 : 0x00;
				ts_write_u8(&ts, res);
				break;
			}

			case CMD_MKDIR: {
				uint8_t nl;
				if(ts_read_exact(&ts, &nl, 1) != 0) goto done;
				char dn[MAX_NAME_LEN + 1] = {0};
				if(nl > MAX_NAME_LEN) nl = MAX_NAME_LEN;
				if(ts_read_exact(&ts, dn, nl) != 0) goto done;
				dn[nl] = 0;

				char path[MAX_PATH_LEN];
				if(!safe_path(ctx.mount_root, dn, path)){
					uint8_t err = 0x01;
					ts_write_u8(&ts, err);
					break;
				}
				int rr = mkdir(path, 0755);
				uint8_t res = rr ? 0x01 : 0x00;
				ts_write_u8(&ts, res);
				break;
			}

			case CMD_RMDIR: {
				uint8_t nl;
				if(ts_read_exact(&ts, &nl, 1) != 0) goto done;
				char dn[MAX_NAME_LEN + 1] = {0};
				if(nl > MAX_NAME_LEN) nl = MAX_NAME_LEN;
				if(ts_read_exact(&ts, dn, nl) != 0) goto done;
				dn[nl] = 0;

				char path[MAX_PATH_LEN];
				if(!safe_path(ctx.mount_root, dn, path)){
					uint8_t err = 0x01;
					ts_write_u8(&ts, err);
					break;
				}
				int rr = rmdir(path);
				uint8_t res = rr ? 0x01 : 0x00;
				ts_write_u8(&ts, res);
				break;
			}

			case CMD_LABEL: {
				const char *L = "UZENETVOL";
				uint8_t len = (uint8_t)strlen(L);
				uint8_t ok = 0x00;
				ts_write_u8(&ts, ok);
				ts_write_u8(&ts, len);
				ts_write(&ts, L, len);
				break;
			}

			case CMD_FREESPACE: {
				struct statvfs fs;
				if(statvfs(ctx.mount_root, &fs) != 0){
					uint8_t err = 0x01;
					ts_write_u8(&ts, err);
					break;
				}
				uint32_t fb = (uint32_t)fs.f_bavail;
				uint32_t bs = (uint32_t)fs.f_frsize;
				uint8_t ok  = 0x00;
				ts_write_u8(&ts, ok);
				ts_write(&ts, &fb, sizeof(fb));
				ts_write(&ts, &bs, sizeof(bs));
				break;
			}

			case CMD_TRUNCATE: {
				uint8_t nl;
				if(ts_read_exact(&ts, &nl, 1) != 0) goto done;
				char fn[MAX_NAME_LEN + 1] = {0};
				if(nl > MAX_NAME_LEN) nl = MAX_NAME_LEN;
				if(ts_read_exact(&ts, fn, nl) != 0) goto done;
				fn[nl] = 0;

				uint32_t ns;
				if(ts_read_exact(&ts, &ns, 4) != 0) goto done;

				char path[MAX_PATH_LEN];
				if(!safe_path(ctx.mount_root, fn, path)){
					uint8_t err = 0x01;
					ts_write_u8(&ts, err);
					break;
				}
				int rr = truncate(path, ns);
				uint8_t res = rr ? 0x01 : 0x00;
				ts_write_u8(&ts, res);
				break;
			}

			default: {
				uint8_t err = 0xFF;
				ts_write_u8(&ts, err);
				break;
			}
		}
	}

done:
	close(ctx.fd);
	return NULL;
}

// -----------------------------------------------------------------------------
// Server entrypoint (UNIX domain socket for uzenet-room)
// -----------------------------------------------------------------------------

static int run_uzenet_fatfs_server(const char *sock_path){
	int sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if(sock < 0) return -1;

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

	unlink(sock_path);
	if(bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0){
		close(sock);
		return -2;
	}
	if(listen(sock, BACKLOG) < 0){
		close(sock);
		return -3;
	}

	for(;;){
		int cfd = accept(sock, NULL, NULL);
		if(cfd < 0){
			if(errno == EINTR) continue;
			break;
		}
		ThreadArg *ta = (ThreadArg*)malloc(sizeof(*ta));
		if(!ta){
			close(cfd);
			continue;
		}
		ta->client_fd = cfd;
		pthread_t tid;
		pthread_create(&tid, NULL, handle_client, ta);
		pthread_detach(tid);
	}
	close(sock);
	return 0;
}

int main(int argc, char *argv[]){
	const char *sock_path = FATFS_SOCKET_PATH;
	if(argc > 1){
		sock_path = argv[1];
	}

	openlog("uzenet_fatfs", LOG_PID, LOG_LOCAL6);
	log_msg("Starting uzenet_fatfs_server on %s", sock_path);
	int res = run_uzenet_fatfs_server(sock_path);
	if(res != 0) log_msg("Server exited with error %d", res);
	closelog();
	return res;
}
