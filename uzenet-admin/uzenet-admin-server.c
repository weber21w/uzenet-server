// uzenet-admin-server.c – with embedded cmark rendering
#define _GNU_SOURCE
#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <crypt.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "cmark_embedded.h"

#define PORT 9460
#define USERS_CSV "/var/lib/uzenet/users.csv"
#define ALLOWLIST "/var/lib/uzenet/allowlist.txt"
#define MAX_LINE 256
#define REALM "Uzenet Admin"

struct user {
	char name13[14], name8[9], name6[7];
	char hash[65];
	char flags;
	char devs[256];
};

static int decode_base64(const char *in, unsigned char *out, size_t outlen){
	static const char tbl[256] = {
		['A']=0,['B']=1,['C']=2,['D']=3,['E']=4,['F']=5,['G']=6,['H']=7,
		['I']=8,['J']=9,['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,
		['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,
		['Y']=24,['Z']=25,['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,
		['g']=32,['h']=33,['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,
		['o']=40,['p']=41,['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,
		['w']=48,['x']=49,['y']=50,['z']=51,['0']=52,['1']=53,['2']=54,['3']=55,
		['4']=56,['5']=57,['6']=58,['7']=59,['8']=60,['9']=61,['+']=62,['/']=63
	};
	size_t len = 0;
	for(size_t i = 0; in[i] && in[i+1] && in[i+2] && in[i+3]; i += 4){
		if(len + 2 >= outlen) return -1;
		unsigned val = (tbl[(int)in[i]] << 18) | (tbl[(int)in[i+1]] << 12) |
					   (tbl[(int)in[i+2]] << 6) | tbl[(int)in[i+3]];
		out[len++] = val >> 16;
		out[len++] = (val >> 8) & 0xFF;
		out[len++] = val & 0xFF;
		if(in[i+2] == '=') len -= 2;
		else if(in[i+3] == '=') len -= 1;
	}
	out[len] = 0;
	return len;
}

static int check_auth(const char *username, const char *password, char *ip, struct user *out_user){
	FILE *fp = fopen(USERS_CSV, "r");
	if(!fp) return 0;
	char line[MAX_LINE];
	while(fgets(line, sizeof(line), fp)){
		struct user u = {0};
		char *tok = strtok(line, ",");
		if(!tok) continue;
		strncpy(u.name13, tok, 13);
		tok = strtok(NULL, ","); if(!tok) continue; strncpy(u.name8, tok, 8);
		tok = strtok(NULL, ","); if(!tok) continue; strncpy(u.name6, tok, 6);
		tok = strtok(NULL, ","); if(!tok) continue; strncpy(u.hash, tok, 64);
		tok = strtok(NULL, ","); if(!tok) continue; u.flags = tok[0];
		tok = strtok(NULL, ","); if(tok) strncpy(u.devs, tok, 255);
		if(strcmp(u.name13, username)==0 || strcmp(u.name8, username)==0 || strcmp(u.name6, username)==0){
			char *crypted = crypt(password, "$5$staticSalt$");
			if(!crypted || strcmp(crypted, u.hash)!=0){
				syslog(LOG_WARNING, "LOGIN FAIL: user=%s ip=%s reason=bad password", username, ip);
				fclose(fp); return 0;
			}
			if(out_user) *out_user = u;
			fclose(fp); return 1;
		}
	}
	syslog(LOG_WARNING, "LOGIN FAIL: user=%s ip=%s reason=user not found", username, ip);
	fclose(fp); return 0;
}

static int send_response(struct MHD_Connection *con, const char *page){
	struct MHD_Response *resp = MHD_create_response_from_buffer(strlen(page), (void*)page, MHD_RESPMEM_MUST_COPY);
	int ret = MHD_queue_response(con, MHD_HTTP_OK, resp);
	MHD_destroy_response(resp);
	return ret;
}

static int send_unauthorized(struct MHD_Connection *con){
	const char *page = "401 Unauthorized";
	struct MHD_Response *resp = MHD_create_response_from_buffer(strlen(page), (void*)page, MHD_RESPMEM_PERSISTENT);
	MHD_add_response_header(resp, "WWW-Authenticate", "Basic realm=\"" REALM "\"");
	int ret = MHD_queue_response(con, MHD_HTTP_UNAUTHORIZED, resp);
	MHD_destroy_response(resp);
	return ret;
}

static int render_markdown_page(struct MHD_Connection *con, const char *path){
	FILE *f = fopen(path, "r");
	if(!f) return send_response(con, "File not found.");
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = malloc(len+1);
	fread(buf, 1, len, f);
	buf[len] = 0;
	fclose(f);

	char *html = cmark_to_html_embedded(buf);
	free(buf);
	if(!html) return send_response(con, "Error rendering markdown.");
	char *page;
	asprintf(&page,
		"<!doctype html><html><head><meta charset='utf-8'>"
		"<title>README</title><style>body{max-width:800px;margin:auto;font-family:sans-serif;}pre{background:#eee;padding:1em;}</style>"
		"</head><body>%s</body></html>", html);
	free(html);
	int r = send_response(con, page);
	free(page);
	return r;
}

static int handle_request(void *cls, struct MHD_Connection *con,
	const char *url, const char *method,
	const char *ver, const char *upload_data,
	size_t *upload_data_size, void **con_cls){

	char ip[64] = "unknown";
	const union MHD_ConnectionInfo *info = MHD_get_connection_info(con, MHD_CONNECTION_INFO_CLIENT_ADDRESS);
	if(info && info->client_addr){
		getnameinfo(info->client_addr, sizeof(struct sockaddr_storage), ip, sizeof(ip), NULL, 0, NI_NUMERICHOST);
	}

	const char *auth = MHD_lookup_connection_value(con, MHD_HEADER_KIND, "Authorization");
	if(!auth || strncmp(auth, "Basic ", 6) != 0) return send_unauthorized(con);

	unsigned char decoded[128] = {0};
	if(decode_base64(auth + 6, decoded, sizeof(decoded)) <= 0) return send_unauthorized(con);

	char *colon = strchr((char*)decoded, ':');
	if(!colon) return send_unauthorized(con);
	*colon = 0;
	char *user = strdup((char*)decoded);
	char *pass = strdup((char*)colon + 1);
	struct user u = {0};
	if(!check_auth(user, pass, ip, &u)){
		free(user); free(pass);
		return send_unauthorized(con);
	}
	free(user); free(pass);
	if(u.flags != 'A' && u.flags != 'F') return send_unauthorized(con);

	if(strncmp(url, "/readme/", 8) == 0){
		char path[256];
		snprintf(path, sizeof(path), "../%s/README.md", url+8);
		return render_markdown_page(con, path);
	}

	const char *page =
	"<!doctype html><title>Uzenet Admin</title><h1>Uzenet Admin Panel</h1>"
	"<ul><li><a href=\"/readme/uzenet-identity\">uzenet-identity</a></li>"
	"<li><a href=\"/readme/uzenet-radio\">uzenet-radio</a></li>"
	"<li><a href=\"/readme/uzenet-fatfs\">uzenet-fatfs</a></li>"
	"<li><a href=\"/readme/uzenet-gameplay\">uzenet-gameplay</a></li>"
	"<li><a href=\"/readme/uzenet-lynx\">uzenet-lynx</a></li>"
	"<li><a href=\"/readme/uzenet-score\">uzenet-score</a></li>"
	"<li><a href=\"/readme/uzenet-zipstream\">uzenet-zipstream</a></li>"
	"<li><a href=\"/readme/uzenet-sim\">uzenet-sim</a></li></ul>"
	"<form method=POST action=/grant><p>Grant Dev Access: <input name=shortname>"
	"<button type=submit>Allow Upload</button></form>"
	"<form method=POST action=/add><p>Add User: name13=<input name=n13> pass=<input name=pass> flags=<input name=flags>"
	"<button type=submit>Add</button></form>"
	"<form method=POST action=/del><p>Remove User: name=<input name=name>"
	"<button type=submit>Remove</button></form>"
	"<form method=POST action=/allowip><p>Allow IP: <input name=ip>"
	"<button type=submit>Allow</button></form>"
	"<p><i>Logged in as admin: access granted.</i>";

	return send_response(con, page);
}

int main(){
	openlog("uzenet-admin", LOG_PID | LOG_NDELAY, LOG_AUTH);
	struct MHD_Daemon *daemon = MHD_start_daemon(
		MHD_USE_SELECT_INTERNALLY, PORT, NULL, NULL,
		&handle_request, NULL,
		MHD_OPTION_END);
	if(!daemon){
		fprintf(stderr, "Could not start server\n");
		return 1;
	}
	printf("Uzenet Admin Server running on port %d\n", PORT);
	getchar();
	MHD_stop_daemon(daemon);
	closelog();
	return 0;
}
