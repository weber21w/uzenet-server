#ifndef UZENET_VIDEO_H
#define UZENET_VIDEO_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

pthread_mutex_t stdout_lock = PTHREAD_MUTEX_INITIALIZER;
int server_fd = -1;

typedef uint8_t  u8;
typedef int8_t   s8;
typedef uint16_t u16;
typedef int16_t  s16;
typedef uint32_t u32;
typedef int32_t  s32;

#define PORT 12345
#define MAX_CLIENTS 8
#define MAX_URL_LEN 1024

volatile int stop_requested = 0;

#define UZENET_VIDEO_WIDTH      256
#define UZENET_VIDEO_HEIGHT     144
#define UZENET_VIDEO_TILE_SIZE  8
#define UZENET_VIDEO_TILE_BYTES 8
#define UZENET_VIDEO_CHUNKS     ((UZENET_VIDEO_WIDTH / UZENET_VIDEO_TILE_SIZE) * (UZENET_VIDEO_HEIGHT / UZENET_VIDEO_TILE_SIZE)) // 576
#define UZENET_VIDEO_BITMAP_LEN (UZENET_VIDEO_CHUNKS / 8) // 72 bytes

#define UZENET_VIDEO_ADPCM_SAMPLES 262
#define UZENET_VIDEO_ADPCM_BITS    (UZENET_VIDEO_ADPCM_SAMPLES) // 1-bit per sample
#define UZENET_VIDEO_ADPCM_BYTES   ((UZENET_VIDEO_ADPCM_BITS + 7) / 8) // 33 bytes
#define UZENET_VIDEO_ADPCM_MAGIC_MASK 0x03
#define UZENET_VIDEO_ADPCM_MAGIC_VAL  0x02

#define UZENET_VIDEO_FRAME_HEADER_LEN (UZENET_VIDEO_ADPCM_BYTES + UZENET_VIDEO_BITMAP_LEN)
#define UZENET_VIDEO_MAX_TILEDATA_LEN (UZENET_VIDEO_CHUNKS * UZENET_VIDEO_TILE_BYTES)
#define UZENET_VIDEO_MAX_FRAME_LEN (UZENET_VIDEO_FRAME_HEADER_LEN + UZENET_VIDEO_MAX_TILEDATA_LEN)

#define UZENET_VIDEO_CMD_URL "URL"
#define UZENET_VIDEO_CMD_MAX "MAX"
#define UZENET_VIDEO_FPS     60
#define UZENET_VIDEO_AUDIO_HZ 16000

#endif // UZENET_VIDEO_H
