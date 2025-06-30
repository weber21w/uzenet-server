#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

#define _XOPEN_SOURCE_EXTENDED 1
#include <sys/ipc.h>
#include <sys/shm.h>

#define RING_BUF_LEN	128*1024
#define SHMSIZE	sizeof(ringBuf_t)*2

struct ringBuf{
	volatile int len;
	volatile int in,out;
	volatile unsigned char buf[RING_BUF_LEN];
};
typedef struct ringBuf ringBuf_t;
struct ringBuf *tx;
struct ringBuf *rx;

int RingWrite(struct ringBuf *r, unsigned char v){
	if(r->len >= RING_BUF_LEN)
		return -1;
	r->buf[r->in++] = v;
	r->in %= RING_BUF_LEN;
	r->len++;
}

int RingRead(struct ringBuf *r){
	if(!r->len)
		return -1;
	unsigned char t = r->buf[r->out++];
	r->out %= RING_BUF_LEN;
	r->len--;
	return t;
}

int RingLength(struct ringBuf *r){
	return r->len;
}

int RingStrLen(struct ringBuf *r){
	if(r->len < 1)
		return -1;
	int rpos = r->out;
	int slen = 0;
	for(int i=0;i<r->len;i++){
		if(r->buf[rpos++] == '\0')
			return slen;
		slen++;
	}
	return -1;//no end to string yet, need to wait for more data
}

void ms_sleep(int count){//why is usleep not POSIX??
		struct timespec reqtime = { 0, 1000 };
		for(int i=0;i<count;i++)
			nanosleep(&reqtime, NULL);
}

pthread_t station_info_thread;
pthread_t stream_audio_thread;
pthread_t search_stations_thread;

void ms_sleep(int count);
int ProcessClient();

void *StationInfo();
void *StreamAudio();
void *SearchStations();
int SelectDatabaseServer();
void CompressAudioFrame(unsigned char *in, int depth);

char *search_url = "https://de1.api.radio-browser.info/json/stations/search";
//nslookup -type=srv _api._tcp.radio-browser.info | grep "_api" | head -n1 | awk '{ print $7 }'
char *search_headers = "-H 'Accept: application/json' -H 'Content-Type: application/json' -H 'User-Agent: UzeboxRadio'";

#define CMD_NONE		0
#define CMD_SEARCH		1
#define CMD_SEARCH_OFFSET	2
#define CMD_SEARCH_LIMIT	3
#define CMD_SEARCH_TAGS		4
#define CMD_SEARCH_HIDE_BROKEN	5
#define CMD_SEARCH_ORDER	6
#define CMD_SEARCH_REVERSE	7
#define CMD_SEARCH_RUN		8
#define CMD_SEARCH_CSV		9

#define CMD_SAMPLES_FRAME2B	20//all sample requests are 128(encoded) samples, send multiple per frame as needed
#define CMD_SAMPLES_FRAME4B	21

unsigned char search_data[128*1024];
char search_json[2048];
int search_offset;//4 bytes used
int search_limit;//2 bytes used
char search_tags[1024];
int search_hide_broken;//1 byte used
int search_order;//1 byte used
int search_reverse;//1 byte used

const char *search_order_strings[] = {
"clickcount",
"name",
"random",
"bitrate",
"lastchecktime",
"clicktimestamp",
"changetimestamp",
"clicktrend",
"votes"
};

char target_station[1024];

char station_info_buf[4096];
char station_info_work[4096];
volatile int station_info_updated = 0;
volatile int station_info_wait = 0;
volatile int station_info_is_waiting = 0;

volatile int stream_new_data = 0;
volatile int stream_connection_failed = 0;
volatile int thread_shutdown = 0;
volatile int stream_pause = 1;
volatile int stream_disconnect = 0;

volatile int stream_audio_thread_done = 0;
volatile int station_info_thread_done = 0;

int new_station_search = 0;

//unsigned char client_cmd_buf[1024];
//int client_buf_pos = 0;
//int client_buf_bytes = 0;
//int client_buf_in = 0;
//int client_buf_out = 0;

unsigned char stream_data_new[262];
unsigned char stream_client_buf[15734*8];
unsigned int stream_buffered_bytes;
unsigned int stream_requested_bytes;

int client_command = 0;

/*
ffprobe -i 'http://hyperadio.ru:8000/live' -hide_banner
[mp3 @ 0x55bc2079bec0] Skipping 402 bytes of junk at 0.
Input #0, mp3, from 'http://hyperadio.ru:8000/live':
  Metadata:
	icy-br          : 128
	icy-description : demoscene radio
	icy-genre       : demoscene, 8bit, vgm, chiptune, tracked
	icy-name        : HYPERADIO
	icy-pub         : 0
	icy-url         : https://hyperadio.retroscene.org
	icy-aim         : https://t.me/hyperadio
	StreamTitle     : Trauma Child Genesis - Traumatique | 05:36
  Duration: N/A, start: 0.000000, bitrate: 128 kb/s
  Stream #0:0: Audio: mp3, 44100 Hz, stereo, fltp, 128 kb/s
*/

/*
curl -s 'https://de1.api.radio-browser.info/json/stations/search' \
  -H 'Accept: application/json' \
  -H 'Content-Type: application/json' \
  -H 'User-Agent: UzeboxRadio' \
  --data-raw '{"offset":0,"limit":10,"tagList":"techno","hidebroken":"true","order":"votes","reverse":"false"}'
 */
 
 /*
 [
   {
	"changeuuid": "8c756219-3a95-4ced-a672-74762f8fe3a6",
	"stationuuid": "441d7702-b003-4668-ab26-589f3a4c4934",
	"serveruuid": null,
	"name": "XS80s",
	"url": "http://s1.myradiostream.com:12508/listen.mp3",
	"url_resolved": "http://s1.myradiostream.com:12508/listen.mp3",
	"homepage": "https://xs80s.com/",
	"favicon": "https://xs80s.com/img/XS80s-PP.png",
	"tags": "1980's,1980s,80's,80s,decades,hits,pop,rock",
	"country": "New Zealand",
	"countrycode": "NZ",
	"iso_3166_2": null,
	"state": "Christchurch",
	"language": "english",
	"languagecodes": "",
	"votes": 25,
	"lastchangetime": "2023-12-28 16:21:25",
	"lastchangetime_iso8601": "2023-12-28T16:21:25Z",
	"codec": "MP3",
	"bitrate": 128,
	"hls": 0,
	"lastcheckok": 1,
	"lastchecktime": "2024-05-22 14:55:01",
	"lastchecktime_iso8601": "2024-05-22T14:55:01Z",
	"lastcheckoktime": "2024-05-22 14:55:01",
	"lastcheckoktime_iso8601": "2024-05-22T14:55:01Z",
	"lastlocalchecktime": "",
	"lastlocalchecktime_iso8601": null,
	"clicktimestamp": "2024-05-22 21:44:26",
	"clicktimestamp_iso8601": "2024-05-22T21:44:26Z",
	"clickcount": 4,
	"clicktrend": 0,
	"ssl_error": 0,
	"geo_lat": null,
	"geo_long": null,
	"has_extended_info": false
  }
]
*/
