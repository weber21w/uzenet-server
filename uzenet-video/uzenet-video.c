#include "uzenet-video.h"

static void stream_video_to_client(int client_fd, const char *url, int max_tiles) {
	AVFormatContext *fmt_ctx = NULL;
	AVCodecContext *video_dec_ctx = NULL, *audio_dec_ctx = NULL;
	AVStream *video_stream = NULL, *audio_stream = NULL;
	SwsContext *sws_ctx = NULL;
	SwrContext *swr_ctx = NULL;
	AVPacket *pkt = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();
	u8 last_tiles[UZENET_VIDEO_CHUNKS * UZENET_VIDEO_TILE_BYTES] = {0};
	u8 frame_buf[UZENET_VIDEO_MAX_FRAME_LEN];
	u8 bitmap[UZENET_VIDEO_BITMAP_LEN];
	u8 *tile_out = frame_buf + UZENET_VIDEO_FRAME_HEADER_LEN;

	if (avformat_open_input(&fmt_ctx, url, NULL, NULL) < 0) {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "FFmpeg error: Failed to open input: %s\n", url);
		pthread_mutex_unlock(&stdout_lock);
		goto cleanup;
	}
	if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "FFmpeg error: Failed to find stream info\n");
		pthread_mutex_unlock(&stdout_lock);
		goto cleanup;
	}

	int video_stream_idx = -1, audio_stream_idx = -1;
	for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
		AVCodecParameters *par = fmt_ctx->streams[i]->codecpar;
		if (par->codec_type == AVMEDIA_TYPE_VIDEO && video_stream_idx < 0) video_stream_idx = i;
		if (par->codec_type == AVMEDIA_TYPE_AUDIO && audio_stream_idx < 0) audio_stream_idx = i;
	}
	if (video_stream_idx < 0 || audio_stream_idx < 0) {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "Missing required streams\n");
		pthread_mutex_unlock(&stdout_lock);
		goto cleanup;
	}
	video_stream = fmt_ctx->streams[video_stream_idx];
	audio_stream = fmt_ctx->streams[audio_stream_idx];

	const AVCodec *video_codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
	video_dec_ctx = avcodec_alloc_context3(video_codec);
	avcodec_parameters_to_context(video_dec_ctx, video_stream->codecpar);
	avcodec_open2(video_dec_ctx, video_codec, NULL);

	const AVCodec *audio_codec = avcodec_find_decoder(audio_stream->codecpar->codec_id);
	audio_dec_ctx = avcodec_alloc_context3(audio_codec);
	avcodec_parameters_to_context(audio_dec_ctx, audio_stream->codecpar);
	avcodec_open2(audio_dec_ctx, audio_codec, NULL);

	sws_ctx = sws_getContext(
		video_dec_ctx->width, video_dec_ctx->height, video_dec_ctx->pix_fmt,
		UZENET_VIDEO_WIDTH, UZENET_VIDEO_HEIGHT, AV_PIX_FMT_GRAY8,
		SWS_FAST_BILINEAR, NULL, NULL, NULL);

	swr_ctx = swr_alloc_set_opts(NULL,
		AV_CH_LAYOUT_MONO, AV_SAMPLE_FMT_U8, UZENET_VIDEO_AUDIO_HZ,
		audio_dec_ctx->channel_layout, audio_dec_ctx->sample_fmt, audio_dec_ctx->sample_rate,
		0, NULL);
	swr_init(swr_ctx);

	while (!stop_requested && av_read_frame(fmt_ctx, pkt) >= 0) {
		if (pkt->stream_index == video_stream_idx) {
			if (avcodec_send_packet(video_dec_ctx, pkt) == 0 && avcodec_receive_frame(video_dec_ctx, frame) == 0) {
				AVFrame *gray = av_frame_alloc();
				int linesize = UZENET_VIDEO_WIDTH;
				gray->format = AV_PIX_FMT_GRAY8;
				gray->width = UZENET_VIDEO_WIDTH;
				gray->height = UZENET_VIDEO_HEIGHT;
				av_image_alloc(gray->data, gray->linesize, gray->width, gray->height, AV_PIX_FMT_GRAY8, 1);
				sws_scale(sws_ctx, (const uint8_t * const *)frame->data, frame->linesize, 0,
					video_dec_ctx->height, gray->data, gray->linesize);

				int change_count = 0;
				memset(bitmap, 0, sizeof(bitmap));
				for (int ty = 0; ty < UZENET_VIDEO_HEIGHT; ty += UZENET_VIDEO_TILE_SIZE) {
					for (int tx = 0; tx < UZENET_VIDEO_WIDTH; tx += UZENET_VIDEO_TILE_SIZE) {
						int tile_index = (ty / 8) * (UZENET_VIDEO_WIDTH / 8) + (tx / 8);
						u8 tile[UZENET_VIDEO_TILE_BYTES];
						for (int y = 0; y < 8; y++) {
							u8 bits = 0;
							for (int x = 0; x < 8; x++) {
								u8 p = gray->data[0][(ty + y) * linesize + (tx + x)];
								if (p > 127) bits |= (1 << (7 - x));
							}
							tile[y] = bits;
						}
						if (memcmp(tile, &last_tiles[tile_index * 8], 8) != 0) {
							if (change_count < max_tiles) {
								memcpy(&tile_out[change_count * 8], tile, 8);
								memcpy(&last_tiles[tile_index * 8], tile, 8);
								bitmap[tile_index / 8] |= (1 << (tile_index % 8));
								change_count++;
							}
						}
					}
				}

				memcpy(frame_buf, bitmap, UZENET_VIDEO_BITMAP_LEN);
				u8 adpcm_audio[UZENET_VIDEO_ADPCM_BYTES] = {0};
				adpcm_audio[0] = UZENET_VIDEO_ADPCM_MAGIC_VAL;
				memcpy(frame_buf + UZENET_VIDEO_BITMAP_LEN, adpcm_audio, UZENET_VIDEO_ADPCM_BYTES);

				int total = UZENET_VIDEO_FRAME_HEADER_LEN + (change_count * UZENET_VIDEO_TILE_BYTES);
				send(client_fd, frame_buf, total, 0);

				av_freep(&gray->data[0]);
				av_frame_free(&gray);
			}
		}
		av_packet_unref(pkt);
	}

cleanup:
	if (video_dec_ctx) avcodec_free_context(&video_dec_ctx);
	if (audio_dec_ctx) avcodec_free_context(&audio_dec_ctx);
	if (sws_ctx) sws_freeContext(sws_ctx);
	if (swr_ctx) swr_free(&swr_ctx);
	if (frame) av_frame_free(&frame);
	if (pkt) av_packet_free(&pkt);
	if (fmt_ctx) avformat_close_input(&fmt_ctx);
}

static void *client_thread(void *arg) {
	int client_fd = *(int *)arg;
	free(arg);

	char recv_buf[2048];
	int recv_len = 0;
	char url[MAX_URL_LEN] = {0};
	int max_changes = -1;

	// Set client socket non-blocking
	fcntl(client_fd, F_SETFL, O_NONBLOCK);

	pthread_mutex_lock(&stdout_lock);
	printf("Client connected\n");
	pthread_mutex_unlock(&stdout_lock);

	while (1) {
		fd_set fds;
		struct timeval tv = {0, 10000}; // 10ms
		FD_ZERO(&fds);
		FD_SET(client_fd, &fds);
		int sel = select(client_fd + 1, &fds, NULL, NULL, &tv);
		if (sel > 0 && FD_ISSET(client_fd, &fds)) {
			char ch;
			int r = read(client_fd, &ch, 1);
			if (r <= 0) break;

			if (recv_len < sizeof(recv_buf) - 1) {
				recv_buf[recv_len++] = ch;
				if (ch == '\n') {
					recv_buf[recv_len] = 0;
					if (recv_len > 1) {
						if (recv_buf[0] == 'U') {
							strncpy(url, recv_buf + 1, MAX_URL_LEN - 1);
							url[strcspn(url, "\r\n")] = 0;
							pthread_mutex_lock(&stdout_lock);
							printf("Received URL: %s\n", url);
							pthread_mutex_unlock(&stdout_lock);
						} else if (recv_buf[0] == 'M') {
							int m = atoi(recv_buf + 1);
							if (m > 0 && m <= UZENET_VIDEO_CHUNKS)
								max_changes = m;
							pthread_mutex_lock(&stdout_lock);
							printf("Received MAX: %d\n", max_changes);
							pthread_mutex_unlock(&stdout_lock);
						} else if (recv_buf[0] == 'S') {
							stop_requested = 1;
							pthread_mutex_lock(&stdout_lock);
							printf("Received STOP\n");
							pthread_mutex_unlock(&stdout_lock);
							break;
						}
					}
					recv_len = 0;
				}
			}
		}
		if (url[0]) break;
	}

	if (!url[0]) {
		pthread_mutex_lock(&stdout_lock);
		printf("No URL provided. Disconnecting client.\n");
		pthread_mutex_unlock(&stdout_lock);
		close(client_fd);
		return NULL;
	}

	if (max_changes < 0) max_changes = UZENET_VIDEO_CHUNKS;
	stop_requested = 0;
	stream_video_to_client(client_fd, url, max_changes);

	pthread_mutex_lock(&stdout_lock);
	printf("Client disconnected\n");
	pthread_mutex_unlock(&stdout_lock);
	close(client_fd);
	return NULL;
}

int main() {
	struct sockaddr_in server_addr;
	char cwd[512];
	getcwd(cwd, sizeof(cwd));
	printf("Current working dir: %s\n", cwd);

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	int opt = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(PORT);

	bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
	listen(server_fd, MAX_CLIENTS);
	printf("uzenet-video listening on port %d\n", PORT);

	while (1) {
		int client_sock = accept(server_fd, NULL, NULL);
		if (client_sock >= 0) {
			int *fd_ptr = malloc(sizeof(int));
			*fd_ptr = client_sock;
			pthread_t tid;
			pthread_create(&tid, NULL, client_thread, fd_ptr);
			pthread_detach(tid);
		}
	}

	return 0;
}
