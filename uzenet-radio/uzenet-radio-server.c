// uzenet-radio-server.c - refactored for uzenet-room domain socket, no login, framed stream
#define _POSIX_C_SOURCE 200809L
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <pthread.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>

#define SOCKET_PATH "/run/uzenet/radio.sock"
#define CMD_MAX 1024
#define CONSUME_RATE 15720.0
#define HIGH_WATER 15720
#define DEFAULT_FRAME_LEN 64
#define MIN_ALLOWED_FRAME_LEN 16
#define MAX_ALLOWED_FRAME_LEN 512

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
	printf("[radio] New client connected (fd=%d)\n", client_fd);

	char cmd[CMD_MAX], url[CMD_MAX];
	int max_frame_len = DEFAULT_FRAME_LEN;
	time_t stream_start = 0, last_meta = 0;
	double total_sent = 0;
	long sleep_ms = 0;

	// Read initial Stream URL
	if(read_line(client_fd, cmd, sizeof(cmd)) <= 0 || strncmp(cmd, "Stream ", 7) != 0){
		printf("[radio] Invalid Stream handshake\n");
		close(client_fd);
		return NULL;
	}
	snprintf(url, sizeof(url), "%s", cmd + 7);
	char *nl = strpbrk(url, "\r\n"); if(nl) *nl = '\0';
	printf("[radio] Stream URL: '%s'\n", url);

	// ---- Init FFmpeg ----
	avformat_network_init();
	AVFormatContext *fmt = NULL;
	AVDictionary *opts = NULL;
	av_dict_set(&opts, "protocol_whitelist", "file,http,https,tcp,tls,crypto", 0);
	av_dict_set(&opts, "user_agent", "uzenet-radio/1.0", 0);

	if(avformat_open_input(&fmt, url, NULL, &opts) < 0){
		fprintf(stderr, "[radio] avformat_open_input failed\n");
		goto cleanup;
	}
	av_dict_free(&opts);
	if(avformat_find_stream_info(fmt, NULL) < 0) goto cleanup;
	int aidx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if(aidx < 0) goto cleanup;

	AVCodecParameters *cp = fmt->streams[aidx]->codecpar;
	AVCodec *codec = avcodec_find_decoder(cp->codec_id);
	AVCodecContext *dec = avcodec_alloc_context3(codec);
	avcodec_parameters_to_context(dec, cp);
	avcodec_open2(dec, codec, NULL);

	SwrContext *swr = swr_alloc_set_opts(NULL,
		AV_CH_LAYOUT_MONO, AV_SAMPLE_FMT_S16, 15720,
		dec->channel_layout, dec->sample_fmt, dec->sample_rate, 0, NULL);
	swr_init(swr);

	AVPacket *pkt = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();
	int16_t *resampled = NULL;
	int max_out = 48000;
	av_samples_alloc((uint8_t**)&resampled, NULL, 1, max_out, AV_SAMPLE_FMT_S16, 0);

	stream_start = time(NULL);
	total_sent = 0;

	while(av_read_frame(fmt, pkt) >= 0){
		if(pkt->stream_index != aidx){ av_packet_unref(pkt); continue; }
		avcodec_send_packet(dec, pkt);
		av_packet_unref(pkt);

		while(avcodec_receive_frame(dec, frame) == 0){
			int out_samples = swr_convert(swr, (uint8_t**)&resampled, max_out, (const uint8_t**)frame->data, frame->nb_samples);
			if(out_samples <= 0) continue;

			uint8_t *buf8 = malloc(out_samples);
			for(int i = 0; i < out_samples; i++)
				buf8[i] = ((int)resampled[i] + 32768) >> 8;

			if(sleep_ms > 0){
				struct timespec ts = { sleep_ms / 1000, (sleep_ms % 1000) * 1000000 };
				nanosleep(&ts, NULL);
				sleep_ms = 0;
			}

			time_t now = time(NULL);
			double expected = (now - stream_start) * CONSUME_RATE;
			double occupancy = total_sent - expected;
			if(occupancy > HIGH_WATER && out_samples > 2){
				int di = out_samples / 2;
				buf8[di] = (buf8[di - 1] + buf8[di + 1]) >> 1;
				memmove(buf8 + di + 1, buf8 + di + 2, out_samples - di - 2);
				out_samples--;
				write_all(client_fd, "Q", 1);
			}

			int ofs = 0;
			while(ofs < out_samples){
				int chunk = out_samples - ofs;
				if(chunk > max_frame_len) chunk = max_frame_len;
				uint8_t hdr[3] = { 'A', chunk >> 8, chunk & 0xFF };
				write_all(client_fd, hdr, 3);
				write_all(client_fd, buf8 + ofs, chunk);
				ofs += chunk;
				total_sent += chunk;
			}
			free(buf8);

			struct pollfd pfd = { .fd = client_fd, .events = POLLIN };
			if(poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)){
				if(read_line(client_fd, cmd, sizeof(cmd)) > 0){
					if(strncmp(cmd, "Sleep ", 6) == 0){
						sleep_ms = strtol(cmd + 6, NULL, 10);
					}else if(strncmp(cmd, "Meta", 4) == 0){
						if(time(NULL) - last_meta >= 5){
							last_meta = time(NULL);
							// metadata stub
						}
					}else if(strncmp(cmd, "SetFrameLen ", 12) == 0){
						int req = atoi(cmd + 12);
						if(req >= MIN_ALLOWED_FRAME_LEN && req <= MAX_ALLOWED_FRAME_LEN){
							max_frame_len = req;
						}
					}
				}
			}
		}
	}

cleanup:
	av_frame_free(&frame);
	av_packet_free(&pkt);
	av_freep(&resampled);
	swr_free(&swr);
	avcodec_free_context(&dec);
	avformat_close_input(&fmt);
	close(client_fd);
	return NULL;
}

int main(void){
	unlink(SOCKET_PATH);
	int sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if(sock < 0){ perror("socket"); return 1; }

	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", SOCKET_PATH);
	if(bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0){ perror("bind"); return 1; }
	listen(sock, 16);
	printf("[radio] Listening on UNIX socket %s\n", SOCKET_PATH);

	while(1){
		int c = accept(sock, NULL, NULL);
		if(c < 0){ if(errno == EINTR) continue; perror("accept"); break; }
		int *p = malloc(sizeof(int)); *p = c;
		pthread_t tid;
		pthread_create(&tid, NULL, client_thread, p);
		pthread_detach(tid);
	}
	close(sock);
	return 0;
}
