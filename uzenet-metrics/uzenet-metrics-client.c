#include "metrics_client.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int metrics_fd = -1;

int metrics_init(const char *socket_path){
	struct sockaddr_un addr;
	metrics_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if(metrics_fd < 0) return -1;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);
	if(connect(metrics_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
		close(metrics_fd);
		metrics_fd = -1;
		return -1;
	}
	return 0;
}

static void metrics_send(const char *fmt, ...){
	if(metrics_fd < 0) return;
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if(n > 0) write(metrics_fd, buf, n);
}

void metrics_gauge(const char *name, double value){
	metrics_send("gauge %s %f\n", name, value);
}

void metrics_counter(const char *name, int delta){
	metrics_send("counter %s %d\n", name, delta);
}

void metrics_close(void){
	if(metrics_fd >= 0) close(metrics_fd);
	metrics_fd = -1;
}
