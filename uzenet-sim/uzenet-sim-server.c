#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>
#include <errno.h>

static void usage(const char *prog){
	fprintf(stderr,
		"Usage: %s --game <GAMECODE> --ipc <SOCKET_PATH>\n"
		"  --game  8-char game identifier (e.g. MEGATR00)\n"
		"  --ipc   path to Unix-domain socket for this instance\n",
		prog);
	exit(1);
}

int main(int argc, char *argv[]){
	char *game = NULL;
	char *ipc  = NULL;

	// Parse args
	for(int i = 1; i < argc; i++){
		if(strcmp(argv[i], "--game") == 0 && i+1 < argc){
			game = argv[++i];
		}else if(strcmp(argv[i], "--ipc") == 0 && i+1 < argc){
			ipc = argv[++i];
		}else{
			usage(argv[0]);
		}
	}
	if(!game || !ipc){
		usage(argv[0]);
	}

	// Ensure the parent directory for the socket exists
	char *ipc_copy = strdup(ipc);
	if(!ipc_copy){
		perror("strdup");
		exit(1);
	}
	char *dir = dirname(ipc_copy);
	if(mkdir(dir, 0755) && errno != EEXIST){
		fprintf(stderr, "Error creating directory '%s': %s\n", dir, strerror(errno));
		free(ipc_copy);
		exit(1);
	}
	free(ipc_copy);

	// Build the sidecar binary path: /usr/local/bin/uzenet-sim-<GAME>
	char binpath[512];
	if(snprintf(binpath, sizeof(binpath),
				 "/usr/local/bin/uzenet-sim-%s", game) >= (int)sizeof(binpath)){
		fprintf(stderr, "Game code too long\n");
		exit(1);
	}

	// Prepare argv for execv
	char *new_argv[] = {
		binpath,
		"--game", game,
		"--ipc",  ipc,
		NULL
	};

	// Replace this process with the sidecar
	execv(binpath, new_argv);
	// If we get here, execv failed
	fprintf(stderr, "Failed to exec '%s': %s\n", binpath, strerror(errno));
	return 1;
}
