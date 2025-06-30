/* uzenet-metrics.c
 *
 * A sidecar metrics server for Uzenet.
 * Listens on a Unix-domain socket for text-based metrics:
 *   "gauge <name> <value>\n"
 *   "counter <name> <delta>\n"
 * Aggregates them in memory, and exposes an HTTP endpoint /metrics
 * on TCP port for scraping (Prometheus-style).  
 *
 * Compile with: gcc -O2 -pthread -lmicrohttpd -o uzenet-metrics uzenet-metrics.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <microhttpd.h>

#define DEFAULT_SOCKET_PATH "/run/uzenet/metrics.sock"
#define DEFAULT_HTTP_PORT 9000
#define MAX_LINE 256

// Metric entry
struct metric {
	char *name;
	double gauge;
	long long counter;
	int is_counter;
	struct metric *next;
};

static struct metric *metrics_head = NULL;
static pthread_mutex_t metrics_lock = PTHREAD_MUTEX_INITIALIZER;

// Find or create metric by name
static struct metric *get_metric(const char *name, int is_counter){
	struct metric *m = metrics_head;
	while(m){
		if(strcmp(m->name, name) == 0 && m->is_counter == is_counter)
			return m;
		m = m->next;
	}
	// create
	m = calloc(1, sizeof(*m));
	m->name = strdup(name);
	m->is_counter = is_counter;
	m->gauge = 0.0;
	m->counter = 0;
	m->next = metrics_head;
	metrics_head = m;
	return m;
}

// Handle a single metrics line
static void process_line(const char *line){
	char type[16], name[128];
	if(sscanf(line, "%15s %127s", type, name) < 2)
		return;
	if(strcmp(type, "gauge") == 0){
		double value;
		if(sscanf(line, "%*s %*s %lf", &value) == 1){
			pthread_mutex_lock(&metrics_lock);
			struct metric *m = get_metric(name, 0);
			m->gauge = value;
			pthread_mutex_unlock(&metrics_lock);
		}
	}else if(strcmp(type, "counter") == 0){
		long long delta;
		if(sscanf(line, "%*s %*s %lld", &delta) == 1){
			pthread_mutex_lock(&metrics_lock);
			struct metric *m = get_metric(name, 1);
			m->counter += delta;
			pthread_mutex_unlock(&metrics_lock);
		}
	}
}

// Thread: accept and read from Unix socket
static void *socket_thread(void *arg){
	const char *sockpath = arg;
	int srvfd, clientfd;
	struct sockaddr_un addr;
	unlink(sockpath);
	srvfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if(srvfd < 0){ perror("socket"); return NULL; }
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, sockpath, sizeof(addr.sun_path)-1);
	if(bind(srvfd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
		perror("bind"); close(srvfd); return NULL;
	}
	if(listen(srvfd, 5) < 0){ perror("listen"); close(srvfd); return NULL; }
	while(1){
		clientfd = accept(srvfd, NULL, NULL);
		if(clientfd < 0){ perror("accept"); continue; }
		char buf[MAX_LINE];
		FILE *f = fdopen(clientfd, "r");
		if(!f){ close(clientfd); continue; }
		while(fgets(buf, sizeof(buf), f)){
			process_line(buf);
		}
		fclose(f); // closes clientfd
	}
	return NULL;
}

// HTTP handler: return metrics in plain text
static int http_handler(void *cls, struct MHD_Connection *connection,
						const char *url, const char *method,
						const char *version, const char *upload_data,
						size_t *upload_data_size, void **con_cls){
	(void)cls; (void)version; (void)upload_data; (void)upload_data_size;
	if(strcmp(url, "/metrics") != 0 || strcmp(method, "GET") != 0)
		return MHD_NO;
	pthread_mutex_lock(&metrics_lock);
	char *response = NULL;
	size_t len = 0;
	struct metric *m = metrics_head;
	while(m){
		char line[256];
		if(m->is_counter)
			snprintf(line, sizeof(line), "%s %lld\n", m->name, m->counter);
		else
			snprintf(line, sizeof(line), "%s %f\n", m->name, m->gauge);
		size_t l = strlen(line);
		response = realloc(response, len + l + 1);
		memcpy(response + len, line, l);
		len += l;
		response[len] = '\0';
		m = m->next;
	}
	pthread_mutex_unlock(&metrics_lock);
	struct MHD_Response *res = MHD_create_response_from_buffer(len,
										(void*)response, MHD_RESPMEM_MUST_FREE);
	MHD_add_response_header(res, "Content-Type", "text/plain; charset=utf-8");
	int ret = MHD_queue_response(connection, MHD_HTTP_OK, res);
	MHD_destroy_response(res);
	return ret;
}

int main(int argc, char *argv[]){
	const char *sockpath = DEFAULT_SOCKET_PATH;
	int http_port = DEFAULT_HTTP_PORT;
	// TODO: parse --ipc and --port arguments
	pthread_t tid;
	if(pthread_create(&tid, NULL, socket_thread, (void*)sockpath) != 0){
		perror("pthread_create"); return 1; }
	// start HTTP server
	struct MHD_Daemon *daemon = MHD_start_daemon(
		MHD_USE_SELECT_INTERNALLY | MHD_USE_THREAD_PER_CONNECTION,
		http_port, NULL, NULL, &http_handler, NULL, MHD_OPTION_END);
	if(!daemon){
		fprintf(stderr, "Failed to start HTTP daemon\n"); return 1; }
	printf("uzenet-metrics running: socket=%s http_port=%d\n", sockpath, http_port);
	// main thread just waits
	pthread_join(tid, NULL);
	MHD_stop_daemon(daemon);
	return 0;
}
