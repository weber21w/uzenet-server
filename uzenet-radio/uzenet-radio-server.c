#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>

#define LISTEN_PORT           30840
#define BACKLOG               16
#define HANDSHAKE_TIMEOUT_SEC 4
#define CMD_MAX               1024
#define CONSUME_RATE          15720.0   // Uzebox samples/sec
#define HIGH_WATER            15720     // 1 second buffer threshold

// buffer-wise placeholder for later compression
static inline void compress_buffer(uint8_t *buf, size_t len){
	// TODO: replace with your real algorithm
	// identity mapping for now:
	// for(size_t i = 0; i < len; i++) buf[i] = buf[i];
}

// Read a line (including '\n') up to maxlen.
static ssize_t read_line(int fd, char *buf, size_t maxlen){
	size_t i = 0;
	while(i + 1 < maxlen){
		char c;
		ssize_t r = read(fd, &c, 1);
		if(r <= 0) return r;
		buf[i++] = c;
		if(c == '\n') break;
	}
	buf[i] = '\0';
	return (ssize_t)i;
}

// Write all data or return error.
static ssize_t write_all(int fd, const void *buf, size_t len){
	size_t total = 0;
	const char *p = buf;
	while(total < len){
		ssize_t w = write(fd, p + total, len - total);
		if(w <= 0) return w;
		total += w;
	}
	return (ssize_t)total;
}

void *client_thread(void *arg){
	int client_fd = *(int*)arg;
	free(arg);
	printf("[DEBUG] New client connected (fd=%d)\n", client_fd);
	fflush(stdout);

	char cmd[CMD_MAX], url[CMD_MAX];
	time_t stream_start = 0;
	double total_sent = 0;
	long sleep_ms = 0;
	time_t last_meta_request = 0;

	while(1){
		// ---- Handshake ----
		printf("[DEBUG] Waiting for Stream handshake...\n"); fflush(stdout);
		struct timeval tv = { HANDSHAKE_TIMEOUT_SEC, 0 };
		setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		ssize_t len = read_line(client_fd, cmd, sizeof(cmd));
		if(len <= 0){
			printf("[DEBUG] Handshake read_line returned %zd, closing\n", len);
			break;
		}
		printf("[DEBUG] Received handshake: '%s'", cmd); fflush(stdout);
		if(strncmp(cmd, "Stream ", 7) != 0){
			printf("[DEBUG] Invalid handshake prefix, closing\n"); fflush(stdout);
			break;
		}
		snprintf(url, sizeof(url), "%s", cmd + 7);
		char *nl = strpbrk(url, "\r\n"); if(nl) *nl = '\0';
		printf("[DEBUG] Stream URL: '%s'\n", url); fflush(stdout);

		// Special 'login' case
		if(strcmp(url, "login") == 0){
			printf("[DEBUG] Received Stream login, waiting for real URL...\n"); fflush(stdout);
			tv.tv_sec = tv.tv_usec = 0;
			setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
			while(1){
				ssize_t lr = read_line(client_fd, cmd, sizeof(cmd));
				if(lr <= 0) goto CLEANUP;
				printf("[DEBUG] Interim command: '%s'", cmd); fflush(stdout);
				if(strncmp(cmd, "Stream ", 7) == 0){
					char newurl[CMD_MAX];
					snprintf(newurl, sizeof(newurl), "%s", cmd + 7);
					char *nl2 = strpbrk(newurl, "\r\n"); if(nl2) *nl2 = '\0';
					if(strcmp(newurl, "login") != 0){
						strcpy(url, newurl);
						printf("[DEBUG] New real URL: '%s'\n", url); fflush(stdout);
						break;
					}
				}
			}
		}

		// reset counters
		stream_start = time(NULL);
		total_sent = 0;
		sleep_ms = 0;
		last_meta_request = 0;

		// disable timeout for streaming
		tv.tv_sec = tv.tv_usec = 0;
		setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

		// ---- Init FFmpeg ----
		printf("[DEBUG] Initializing FFmpeg for '%s'...\n", url); fflush(stdout);
		AVFormatContext *fmt = NULL;

	AVDictionary *opts = NULL;
	av_dict_set(&opts, "protocol_whitelist", "file,http,https,tcp,tls,crypto", 0);
	av_dict_set(&opts, "user_agent",         "uzenet-radio/1.0",        0);

	if(avformat_open_input(&fmt, url, NULL, &opts) < 0){
		fprintf(stderr, "[ERROR] avformat_open_input failed for %s\n", url);
		av_dict_free(&opts);
		break;
	}
	av_dict_free(&opts);

	if(avformat_find_stream_info(fmt, NULL) < 0){
		fprintf(stderr, "[ERROR] avformat_find_stream_info failed\n");
		avformat_close_input(&fmt);
		break;
	}
	int aidx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if(aidx < 0){
		fprintf(stderr, "[ERROR] No audio stream\n");
		avformat_close_input(&fmt);
		break;
	}

		if(aidx < 0){ printf("[ERROR] No audio stream\n"); avformat_close_input(&fmt); break; }
		printf("[DEBUG] Found audio stream index %d\n", aidx); fflush(stdout);

		AVCodecParameters *cp = fmt->streams[aidx]->codecpar;
		AVCodec *codec = avcodec_find_decoder(cp->codec_id);
		AVCodecContext *dec = avcodec_alloc_context3(codec);
		avcodec_parameters_to_context(dec, cp);
		avcodec_open2(dec, codec, NULL);

		// resample to 15720 Hz
		SwrContext *swr = swr_alloc_set_opts(NULL,
			AV_CH_LAYOUT_MONO, AV_SAMPLE_FMT_S16, 15720,
			dec->channel_layout, dec->sample_fmt, dec->sample_rate,
			0, NULL);
		swr_init(swr);

		AVPacket *pkt = av_packet_alloc();
		AVFrame *frame = av_frame_alloc();
		int16_t *resampled = NULL;
		int max_out = 48000;
		av_samples_alloc((uint8_t**)&resampled, NULL, 1, max_out, AV_SAMPLE_FMT_S16, 0);

		// ---- Streaming loop ----
		while(1){
			if(av_read_frame(fmt, pkt) < 0){ printf("[DEBUG] EOF from av_read_frame\n"); break; }
			if(pkt->stream_index != aidx){ av_packet_unref(pkt); continue; }
			avcodec_send_packet(dec, pkt);
			av_packet_unref(pkt);

			while(avcodec_receive_frame(dec, frame) == 0){
				int out_samples = swr_convert(swr,
					(uint8_t**)&resampled, max_out,
					(const uint8_t**)frame->data, frame->nb_samples);
				if(out_samples <= 0) continue;

				printf("[DEBUG] Decoded %d samples\n", out_samples); fflush(stdout);
				uint8_t *buf8 = malloc(out_samples);
				for(int i = 0; i < out_samples; i++)
					buf8[i] = ((int)resampled[i] + 32768) >> 8;

				// sleep if requested
				if(sleep_ms > 0){
					printf("[DEBUG] Sleeping %ld ms\n", sleep_ms); fflush(stdout);
					struct timespec ts = { sleep_ms/1000, (sleep_ms%1000)*1000000 };
					nanosleep(&ts, NULL);
					sleep_ms = 0;
				}

				// occupancy
				time_t now = time(NULL);
				double expected = (now - stream_start) * CONSUME_RATE;
				double occupancy = total_sent - expected;
				if(occupancy > HIGH_WATER && out_samples > 2){
					int di = out_samples / 2;
					buf8[di] = (buf8[di-1] + buf8[di+1])>>1;
					memmove(buf8+di+1, buf8+di+2, out_samples-di-2);
					out_samples--;
					write_all(client_fd, "Q", 1);
					printf("[DEBUG] Sent Q hint\n"); fflush(stdout);
				}

				// send audio
				uint8_t ahdr[3] = { 'A', (uint8_t)(out_samples>>8), (uint8_t)out_samples };
				write_all(client_fd, ahdr, 3);
				write_all(client_fd, buf8, out_samples);
				total_sent += out_samples;
				printf("[DEBUG] Sent A chunk %d bytes (total_sent=%.0f)\n", out_samples, total_sent);
				fflush(stdout);
				free(buf8);

				// client commands
				struct pollfd pfd = { .fd=client_fd, .events=POLLIN };
				if(poll(&pfd,1,0)>0 && (pfd.revents & POLLIN)){
					ssize_t r = read_line(client_fd, cmd, sizeof(cmd));
					printf("[DEBUG] Received client cmd: '%s'", cmd);
					fflush(stdout);
					if(r>0){
						if(!strncmp(cmd,"Stream ",7)){ printf("[DEBUG] Stream cmd, ending stream\n"); fflush(stdout); goto STREAM_END; }
						else if(!strncmp(cmd,"Scan ",5)){
							printf("[DEBUG] Scan command\n"); fflush(stdout);
							// ... handle scan ...
						}
						else if(!strncmp(cmd,"Sleep ",6)){
							sleep_ms = strtol(cmd+6,NULL,10);
							printf("[DEBUG] Set sleep_ms=%ld\n", sleep_ms); fflush(stdout);
						}
						else if(!strncmp(cmd,"Meta",4)){
							time_t now_req = time(NULL);
							if(now_req - last_meta_request >= 5){
								last_meta_request = now_req;
								printf("[DEBUG] Meta command, sending metadata\n"); fflush(stdout);
								// ... send metadata ...
							}
						}
					}
				}
			}
		}
	STREAM_END:
		printf("[DEBUG] Cleaning up FFmpeg\n"); fflush(stdout);
		av_frame_free(&frame);
		av_packet_free(&pkt);
		av_freep(&resampled);
		swr_free(&swr);
		avcodec_free_context(&dec);
		avformat_close_input(&fmt);
	}
CLEANUP:
	printf("[DEBUG] Closing client_fd %d\n", client_fd); fflush(stdout);
	close(client_fd);
	return NULL;
}

int main(void){
	avformat_network_init();
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock < 0){ perror("socket"); return 1; }
	int opt=1; setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

	struct sockaddr_in addr = { .sin_family=AF_INET,.sin_port=htons(LISTEN_PORT),.sin_addr.s_addr=INADDR_ANY};
	if(bind(sock,(struct sockaddr*)&addr,sizeof(addr))<0){perror("bind");return 1;}
	listen(sock,BACKLOG);
	printf("[DEBUG] Server listening on port %d...\n", LISTEN_PORT); fflush(stdout);

	while(1){
		struct sockaddr_in cli; socklen_t len=sizeof(cli);
		int c = accept(sock,(struct sockaddr*)&cli,&len);
		if(c<0){ if(errno==EINTR) continue; perror("accept"); break; }
		pthread_t tid; int *p=malloc(sizeof(int));*p=c;
		pthread_create(&tid,NULL,client_thread,p);
		pthread_detach(tid);
	}
	close(sock);
	return 0;
}