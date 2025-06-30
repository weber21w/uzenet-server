#include "uzenet-fatfs-server.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/socket.h>
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
#pragma GCC diagnostic ignored "-Wformat-truncation"
// Globals
// -----------------------------------------------------------------------------

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
				
snprintf(path, sizeof(path), "%s/%.*s", uq->base_path, (int)(sizeof(path) - strlen(uq->base_path) - 2), e->d_name);
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
			int ready = uq->ready;
			uint64_t used = uq->usage_bytes;
			uint32_t files = uq->file_count;
			pthread_mutex_unlock(&uq->lock);
			if(!ready) return -3;
			if(used + new_bytes > USER_QUOTA_BYTES){
				log_msg("'%s' over quota: %llu + %llu > %llu",
						user, (unsigned long long)used,
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
// Thread argument
// -----------------------------------------------------------------------------

typedef struct {
	int               client_fd;
	struct sockaddr_in client_addr;
} ThreadArg;

// -----------------------------------------------------------------------------
// Client handler
// -----------------------------------------------------------------------------

static void *handle_client(void *arg){
	ThreadArg *ta = arg;
	ClientContext ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.fd = ta->client_fd;
	inet_ntop(AF_INET, &ta->client_addr.sin_addr, ctx.client_ip, sizeof(ctx.client_ip));
	strncpy(ctx.mount_root, GUEST_DIR, MAX_PATH_LEN);
	strncpy(ctx.user_id,    "guest",   PASSWORD_LEN);
	ctx.is_guest = 1;
	free(ta);

	// Handshake
	fd_set rfds;
	struct timeval tv = { HANDSHAKE_TIMEOUT_SECS, 0 };
	FD_ZERO(&rfds);
	FD_SET(ctx.fd, &rfds);
	if(select(ctx.fd + 1, &rfds, NULL, NULL, &tv) != 1){
		close(ctx.fd);
		log_msg("[%s] Handshake timeout", ctx.client_ip);
		return NULL;
	}
	char hb[sizeof(HANDSHAKE_STRING)];
	if(recv(ctx.fd, hb, sizeof(hb), MSG_WAITALL) != (int)sizeof(hb) ||
		strncmp(hb, HANDSHAKE_STRING, sizeof(hb)) != 0){
		log_msg("[%s] Invalid handshake", ctx.client_ip);
		close(ctx.fd);
		return NULL;
	}

	// Command loop
	for(;;){
		uint8_t cmd;
		if(recv(ctx.fd, &cmd, 1, MSG_WAITALL) != 1) break;
		switch (cmd){
			case CMD_MOUNT: {
				uint8_t len; recv(ctx.fd, &len, 1, MSG_WAITALL);
				char rel[MAX_PATH_LEN+1]; recv(ctx.fd, rel, len, MSG_WAITALL); rel[len]=0;
				char nr[MAX_PATH_LEN];
				if(!safe_path(ctx.mount_root, rel, nr) ||
					access(nr, R_OK|X_OK) != 0){
					send(ctx.fd, "\x01",1,0);
					log_msg("[%s] MOUNT fail: %s", ctx.client_ip, rel);
				}else{
					strncpy(ctx.mount_root, nr, MAX_PATH_LEN);
					send(ctx.fd, "\x00",1,0);
					log_msg("[%s] MOUNT -> %s", ctx.client_ip, nr);
				}
				break;
			}

			case CMD_READDIR: {
				DIR *d = opendir(ctx.mount_root);
				if(!d){ send(ctx.fd, "\x01",1,0); break; }
				struct dirent *e;
				while((e = readdir(d))){
					if(!strcmp(e->d_name,".")||!strcmp(e->d_name,"..")) continue;
					uint8_t nl = strlen(e->d_name);
					send(ctx.fd, &nl,1,0);
					send(ctx.fd, e->d_name, nl, 0);
					char p[MAX_PATH_LEN];
					snprintf(p,sizeof(p),"%s/%s",ctx.mount_root,e->d_name);
					struct stat st; stat(p,&st);
					uint32_t sz = (uint32_t)st.st_size;
					uint8_t attr = S_ISDIR(st.st_mode)?0x10:0x00;
					send(ctx.fd,&sz,sizeof(sz),0);
					send(ctx.fd,&attr,sizeof(attr),0);
					if(ctx.enable_hash){
						uint16_t h=crc16_xmodem((uint8_t*)e->d_name,nl);
						send(ctx.fd,&h,sizeof(h),0);
					}
				}
				closedir(d);
				send(ctx.fd,"\x00",1,0);
				log_msg("[%s] READDIR on %s", ctx.client_ip, ctx.mount_root);
				break;
			}

			case CMD_OPEN: {
				uint8_t nl; recv(ctx.fd,&nl,1,MSG_WAITALL);
				char fn[MAX_NAME_LEN+1]={0}; recv(ctx.fd,fn,nl,MSG_WAITALL); fn[nl]=0;
				char path[MAX_PATH_LEN];
				if(!safe_path(ctx.mount_root, fn, path)){
					send(ctx.fd,"\x01",1,0);
					break;
				}
				if(ctx.open_file) fclose(ctx.open_file);
				ctx.open_file = fopen(path, ctx.is_guest?"rb":"r+b");
				uint8_t r = ctx.open_file?0x00:0x01;
				send(ctx.fd,&r,1,0);
				log_msg("[%s] OPEN %s -> %s", ctx.client_ip, fn, r?"FAIL":"OK");
				break;
			}

			case CMD_READ: {
				uint32_t off; uint16_t len;
				recv(ctx.fd,&off,sizeof(off),MSG_WAITALL);
				recv(ctx.fd,&len,sizeof(len),MSG_WAITALL);
				if(!ctx.open_file){ send(ctx.fd,"\x02",1,0); break; }
				fseek(ctx.open_file, off, SEEK_SET);
				uint8_t buf[MAX_READ_SIZE];
				size_t rd = fread(buf,1,len,ctx.open_file);
				uint8_t ok=0x00; send(ctx.fd,&ok,1,0);
				uint16_t rl = rd; send(ctx.fd,&rl,sizeof(rl),0);
				send(ctx.fd,buf,rd,0);
				log_msg("[%s] READ %u@%u", ctx.client_ip, rl, off);
				break;
			}

			case CMD_LSEEK: {
				uint32_t off; recv(ctx.fd,&off,sizeof(off),MSG_WAITALL);
				if(!ctx.open_file){ send(ctx.fd,"\x02",1,0); break; }
				ctx.current_offset = off;
				send(ctx.fd,"\x00",1,0);
				break;
			}

			case CMD_CLOSE:
				if(ctx.open_file){ fclose(ctx.open_file); ctx.open_file=NULL; }
				send(ctx.fd,"\x00",1,0);
				break;

			case CMD_OPTS: {
				uint8_t opt; uint32_t val;
				recv(ctx.fd,&opt,1,MSG_WAITALL);
				recv(ctx.fd,&val,4,MSG_WAITALL);
				if(opt==1) ctx.enable_lfn  = val;
				if(opt==2) ctx.enable_crc  = val;
				if(opt==3) ctx.enable_hash = val;
				send(ctx.fd,"\x00",1,0);
				break;
			}

			case CMD_GETOPT: {
				uint8_t flags = (ctx.enable_lfn?1:0)
							  | (ctx.enable_crc?2:0)
							  | (ctx.enable_hash?4:0);
				send(ctx.fd,&flags,1,0);
				break;
			}

			case CMD_HASHINDEX: {
				DIR *d = opendir(ctx.mount_root);
				if(!d){ send(ctx.fd,"\x01",1,0); break; }
				struct dirent *e;
				while((e = readdir(d))){
					if(e->d_type!=DT_REG) continue;
					uint8_t nl=strlen(e->d_name);
					uint16_t h=crc16_xmodem((uint8_t*)e->d_name,nl);
					send(ctx.fd,&nl,1,0);
					send(ctx.fd,e->d_name,nl,0);
					send(ctx.fd,&h,sizeof(h),0);
				}
				closedir(d);
				send(ctx.fd,"\x00",1,0);
				break;
			}

			case CMD_STAT: {
				uint8_t nl; recv(ctx.fd,&nl,1,MSG_WAITALL);
				char fn[MAX_NAME_LEN+1]={0}; recv(ctx.fd,fn,nl,MSG_WAITALL); fn[nl]=0;
				char path[MAX_PATH_LEN];
				if(!safe_path(ctx.mount_root, fn, path)){
					send(ctx.fd,"\x01",1,0); break;
				}
				struct stat st;
				if(stat(path,&st)!=0){
					send(ctx.fd,"\x01",1,0);
				}else{
					uint8_t ok=0x00;
					uint32_t sz=st.st_size;
					uint8_t at=S_ISDIR(st.st_mode)?0x10:0x00;
					send(ctx.fd,&ok,1,0);
					send(ctx.fd,&sz,sizeof(sz),0);
					send(ctx.fd,&at,sizeof(at),0);
				}
				break;
			}

			case CMD_TIME: {
				uint8_t ok=0x00; uint32_t now=time(NULL);
				send(ctx.fd,&ok,1,0);
				send(ctx.fd,&now,sizeof(now),0);
				break;
			}

			case CMD_RENAME: {
				uint8_t l1,l2;
				recv(ctx.fd,&l1,1,MSG_WAITALL);
				char o[MAX_NAME_LEN+1]={0}; recv(ctx.fd,o,l1,MSG_WAITALL); o[l1]=0;
				recv(ctx.fd,&l2,1,MSG_WAITALL);
				char n[MAX_NAME_LEN+1]={0}; recv(ctx.fd,n,l2,MSG_WAITALL); n[l2]=0;
				char p1[MAX_PATH_LEN],p2[MAX_PATH_LEN];
				if(!safe_path(ctx.mount_root,o,p1) ||
					!safe_path(ctx.mount_root,n,p2)){
					send(ctx.fd,"\x01",1,0); break;
				}
				int r = rename(p1,p2);
				uint8_t res = r?0x01:0x00;
				send(ctx.fd,&res,1,0);
				break;
			}

			case CMD_CREATE: {
				uint8_t nl; recv(ctx.fd,&nl,1,MSG_WAITALL);
				char fn[MAX_NAME_LEN+1]={0}; recv(ctx.fd,fn,nl,MSG_WAITALL); fn[nl]=0;
				char path[MAX_PATH_LEN];
				if(!safe_path(ctx.mount_root, fn, path)){
					send(ctx.fd,"\x01",1,0); break;
				}
				int qc = quota_check(ctx.user_id,0,1);
				if(qc==-2){ send(ctx.fd,"\xFE",1,0); break; }
				int fd=open(path,O_CREAT|O_EXCL|O_WRONLY,0644);
				uint8_t r = fd>=0?0x00:0xFF;
				if(fd>=0) close(fd);
				send(ctx.fd,&r,1,0);
				break;
			}

			case CMD_WRITE: {
				uint8_t nl; recv(ctx.fd,&nl,1,MSG_WAITALL);
				char fn[MAX_NAME_LEN+1]={0}; recv(ctx.fd,fn,nl,MSG_WAITALL); fn[nl]=0;
				uint16_t len; recv(ctx.fd,&len,2,MSG_WAITALL);
				if(len>MAX_READ_SIZE){ send(ctx.fd,"\xFD",1,0); break; }
				uint8_t buf[MAX_READ_SIZE];
				recv(ctx.fd,buf,len,MSG_WAITALL);
				char path[MAX_PATH_LEN];
				if(!safe_path(ctx.mount_root, fn, path)){
					send(ctx.fd,"\x01",1,0); break;
				}
				int qc = quota_check(ctx.user_id,len,0);
				if(qc==-1){ send(ctx.fd,"\xFC",1,0); break; }
				int fd=open(path,O_WRONLY|O_APPEND);
				uint8_t r=0xFF;
				if(fd>=0){
					if(write(fd,buf,len)==len) r=0x00;
					close(fd);
				}
				send(ctx.fd,&r,1,0);
				break;
			}

			case CMD_DELETE: {
				uint8_t nl; recv(ctx.fd,&nl,1,MSG_WAITALL);
				char fn[MAX_NAME_LEN+1]={0}; recv(ctx.fd,fn,nl,MSG_WAITALL); fn[nl]=0;
				char path[MAX_PATH_LEN];
				if(!safe_path(ctx.mount_root, fn, path)){
					send(ctx.fd,"\x01",1,0); break;
				}
				int r = remove(path);
				uint8_t res = r?0x01:0x00;
				send(ctx.fd,&res,1,0);
				break;
			}

			case CMD_MKDIR: {
				uint8_t nl; recv(ctx.fd,&nl,1,MSG_WAITALL);
				char dn[MAX_NAME_LEN+1]={0}; recv(ctx.fd,dn,nl,MSG_WAITALL); dn[nl]=0;
				char path[MAX_PATH_LEN];
				if(!safe_path(ctx.mount_root, dn, path)){
					send(ctx.fd,"\x01",1,0); break;
				}
				int r = mkdir(path,0755);
				uint8_t res = r?0x01:0x00;
				send(ctx.fd,&res,1,0);
				break;
			}

			case CMD_RMDIR: {
				uint8_t nl; recv(ctx.fd,&nl,1,MSG_WAITALL);
				char dn[MAX_NAME_LEN+1]={0}; recv(ctx.fd,dn,nl,MSG_WAITALL); dn[nl]=0;
				char path[MAX_PATH_LEN];
				if(!safe_path(ctx.mount_root, dn, path)){
					send(ctx.fd,"\x01",1,0); break;
				}
				int r = rmdir(path);
				uint8_t res = r?0x01:0x00;
				send(ctx.fd,&res,1,0);
				break;
			}

			case CMD_LABEL: {
				const char *L="UZENETVOL";
				uint8_t len=strlen(L), ok=0x00;
				send(ctx.fd,&ok,1,0);
				send(ctx.fd,&len,1,0);
				send(ctx.fd,L,len,0);
				break;
			}

			case CMD_FREESPACE: {
				struct statvfs fs;
				if(statvfs(ctx.mount_root,&fs)!=0){
					send(ctx.fd,"\x01",1,0);
					break;
				}
				uint32_t fb=fs.f_bavail, bs=fs.f_frsize, ok=0x00;
				send(ctx.fd,&ok,1,0);
				send(ctx.fd,&fb,sizeof(fb),0);
				send(ctx.fd,&bs,sizeof(bs),0);
				break;
			}

			case CMD_TRUNCATE: {
				uint8_t nl; recv(ctx.fd,&nl,1,MSG_WAITALL);
				char fn[MAX_NAME_LEN+1]={0}; recv(ctx.fd,fn,nl,MSG_WAITALL); fn[nl]=0;
				uint32_t ns; recv(ctx.fd,&ns,4,MSG_WAITALL);
				char path[MAX_PATH_LEN];
				if(!safe_path(ctx.mount_root,fn,path)){
					send(ctx.fd,"\x01",1,0); break;
				}
				int r = truncate(path, ns);
				uint8_t res=r?0x01:0x00;
				send(ctx.fd,&res,1,0);
				break;
			}

			default:
				send(ctx.fd, "\xFF", 1, 0);
				break;
		}
	}

	close(ctx.fd);
	return NULL;
}

// -----------------------------------------------------------------------------
// Server entrypoint
// -----------------------------------------------------------------------------

int start_uzenet_fatfs_server(int port){
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock < 0) return -1;
	int opt = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in srv = {
		.sin_family = AF_INET,
		.sin_port   = htons(port),
		.sin_addr.s_addr = INADDR_ANY
	};
	if(bind(sock, (struct sockaddr*)&srv, sizeof(srv)) < 0) return -2;
	if(listen(sock, BACKLOG) < 0) return -3;

	while(1){
		ThreadArg *ta = malloc(sizeof(*ta));
		socklen_t len = sizeof(ta->client_addr);
		ta->client_fd = accept(sock, (struct sockaddr*)&ta->client_addr, &len);
		if(ta->client_fd < 0){ free(ta); continue; }
		pthread_t tid;
		pthread_create(&tid, NULL, handle_client, ta);
		pthread_detach(tid);
	}
	return 0;
}

int main(int argc, char *argv[]){
	int port = 57428;
	if(argc > 1) port = atoi(argv[1]);
	openlog("uzenet_fatfs", LOG_PID, LOG_LOCAL6);
	log_msg("Starting uzenet_fatfs_server on port %d", port);
	int res = start_uzenet_fatfs_server(port);
	if(res != 0) log_msg("Server exited with error %d", res);
	closelog();
	return res;
}
