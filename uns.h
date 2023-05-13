#include <pthread.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/socket.h>

char *banner = "\n"
"█====██=███████▒█████=███▄====█=▓█████▄███████\n"
"██  ▓██▒ ▒ ▒ ▄▀░█   ▀ ██ ▀█   █ ▓█   ▀▓  ██▒▓|\n"
"██  ▒██░ ▒ ▄▀▒░ ███  ▓██  ▀█ ██▒▒███  ▒ ▓██░▒|\n"
"▓█  ░██░ ▄▀▒   ░▓█  ▄▓██▒  ▐▌██▒▒▓█  ▄░ ▓██▓░|\n"
"▒█████▓ ███████▒▒████▒██░   ▓██░░▒████▒ ▒██▒░|\n"
"|▒▓▒ ▒ ▒ ▒▒ ▓░▒░▒░ ▒░ ░ ▒░   ▒ ▒ ░░ ▒░ ░ ▒ ░░|\n"
"| ██████ █████ ██▀███░ ██▒   █▓ █████ ▄██▀███|\n"
"|██    ▒ █   ▀▓██ ▒ ██▒▓██░   █▒█   ▀▓██ ▒ ██|\n"
"| ▓██▄   ███  ▓██ ░▄█ ▒ ▓██  █▒░███  ▓██ ░▄█ |\n"
"| ▒   ██▒▓█  ▄▒██▀▀█▄    ▒██ █░░▓█  ▄▒██▀▀█▄ |\n"
"|██████▒▒▒████░██▓ ▒██▒   ▒▀█░  ▒████░██▓ ▒██|\n"
"| ▒▓▒ ▒ ░░ ▒░ ░ ▒▓ ░▒▓░   ░ ▐░  ░░ ▒░░▒▓ ░▒▓░|\n"
"[=====░====░=░===░=====V0.8==░=====░==░===░==]\n\n";

#define MAX_EMULATORS		256
#define EMULATER_STOPPED	0
#define EMULATER_RUNNING	1
#define EMULATER_CLOSING	2

#define MAX_MATCH_LFSR	6


#define UN_CMD_NULL			0//0x00:(NULL, as a command this will cause the server to disconnect assumed as error)
#define UN_CMD_HEADER_START		1//0x01:(start of header)
#define UN_CMD_TEXT_START		2//0x02:(start of text)
#define UN_CMD_TEXT_END		3//0x03:(end of text)
#define UN_CMD_TX_END			4//0x04:(end of transmission)disconnects from the server
#define UN_CMD_ENQUIRY			5//0x05:(enquiry)
#define UN_CMD_ACKNOWLEDGE		6//0x06:(acknowledge)
#define UN_CMD_BELL			7//0x07:(bell)
#define UN_CMD_BACKSPACE		8//0x08:(backspace)
#define UN_CMD_HORIZONTAL_TAB		9//0x09:(horizontal tab)
#define UN_CMD_LINE_FEED		10//0x0A:(line feed)
#define UN_CMD_VERTICAL_TAB		11//0x0B:(vertical tab)
#define UN_CMD_FORM_FEED		12//0x0C:(form feed)
#define UN_CMD_CARRIAGE_RETURN	13//0x0D:(carriage return)
#define UN_CMD_SHIFT_OUT		14//0x0E:(shift out)
#define UN_CMD_SHIFT_IN		15//0x0F:(shift in)
#define UN_CMD_DATA_LINK_ESCAPE	16//0x10:(data link escape)
#define UN_CMD_DEVICE_CONTROL1	17//0x11:(device control 1)
#define UN_CMD_DEVICE_CONTROL2	18//0x12:(device control 2)
#define UN_CMD_DEVICE_CONTROL3	19//0x13:(device control 3)
#define UN_CMD_DEVICE_CONTROL4	20//0x14:(device control 4)
#define UN_CMD_NEGATIVE_ACKNOWLEDGE	21//0x15:(negative acknowledge)
#define UN_CMD_SYNCHRONIZE		22//0x16:(synchronize)
#define UN_CMD_END_OF_TX_BLOCK	23//0x17:(end of transmission block)
#define UN_CMD_CANCEL			24//0x18:(cancel)
#define UN_CMD_END_OF_MEDIUM		25//0x19:(end of medium)
#define UN_CMD_SUBSTITUTE		26//0x1A:(substitute)
#define UN_CMD_ESCAPE			27//0x1B:(escape)
#define UN_CMD_FILE_SEPARATOR		28//0x1C:(file separator)
#define UN_CMD_GROUP_SEPARATOR	29//0x1D:(group separator)
#define UN_CMD_RECORD_SEPARATOR	30//0x1E:(record separator)
#define UN_CMD_UNIT_SEPARATOR		31//0x1F:(unit separator)

#define UN_CMD_ROM_IDENTIFY		32//' '
#define UN_FONT_SPECIFICATION_COMMON	33//'!'
#define UN_CMD_FONT_SPECIFY		34//'"'
#define UN_CMD_CHECK_RSVP		35//'#'
#define UN_CMD_JOIN_MATCH		36//'$'
#define UN_CMD_REQ_MATCH_SIMPLE	37//'%'
#define UN_CMD_CHECK_MATCH_READY	38
#define UN_CMD_SEND_MATCH_READY	39
#define UN_CMD_PLAYER_INFO_SIMPLE	40

#define UN_CMD_PAD_DATA_SIMPLE	60





typedef struct{
	pthread_t handle;
	int room;
	int state;

}EmuInst_t;

EmuInst_t emulators[MAX_EMULATORS];

#define USER_KEY_LEN	8 /* this number based on EEPROM limitations */
#define USER_LONG_KEY_LEN	256
#define USER_STORED_DATA_LEN	2048 /* let the user keep some data around, for whatever purposes some advanced game might have */
#define MAX_PLAYERS	256
#define MAX_USERS	1024 /* registerd users, not simulatanously conneted */
typedef struct{

	char name[32]; /* user can change this after logging in (key file will be updated with name, encryption data stays the same) */
	char short_name[16];
	char country[32];
	int time_zone;
	int reservation_room;
	struct timeval reservation_expire;
	struct timeval join_date;
	unsigned char short_key[USER_KEY_LEN+1];
	unsigned char long_key[USER_LONG_KEY_LEN+1];
	unsigned char stored_data[USER_STORED_DATA_LEN+1]; 

}User_t;

User_t users[MAX_USERS];

//TODO TODO DONT JUST KEEP TRACK OF LOGIN ATTEMPTS....KEEP TRACK OF THE CREDENTIALS ATTEMPTED SO THAT REPEATED REQUEST WITHOUT CHANGING CREDENTIALS ARENT BLOCKING LEGIT USERS WHO MADE A MISTAKE...TODO TODO

typedef struct{
	
	int state;
	uint64_t connection_time;
	int sub_state;
	int command;
	int counter;
	int timer[5]; /* client count down timer values, which generate a message when reaching 0(in MS) */
	int timer_state[5]; /* 0 = once, > 0 = continuous, which resets to this value */
	int user;
	int delay;
	int ping;
	int preferences;
	int socket;

	unsigned char challenge[USER_KEY_LEN+1]; /* random bytes, the client must use to look up values in their stored key */
	struct timeval connected_at; /* Linux epoch time the player first connected */
	struct timeval last_activity; /* last time the player sent meaningful data */
	uint8_t ipv4[4];
	char ipv4s[16];
	int mtu;
	uint8_t subscribed[MAX_PLAYERS]; /* players that we will receive messages from */
	uint8_t block_list[16]; /* user IDs that we wont receive messages from while in initial room 0 */
	uint32_t padstate_history[256]; /* most games will probably use 1 byte, but it can change at any time so we just store 4 */
	int padstate_history_pos;

	int match;
	pthread_t async_task_thread; /* used for various purposes that would otherwise unfairly cause server blocking */
	char din[4096];/* data pipeline coming in from client */
	int din_count;
	int din_pos;
	int din_end;
	char dout[16*1024];/* data going out to client */
	int dout_count;
	int dout_pos;
	int dout_end;
	int din_total;
	int dout_total;
	int idle_time;
	int telnet_state;
	uint32_t waiting_thread;
	char temp[128];
	char filename[128];
	char font_translate[256];
	char match_password[32];
	char rom_name[16];
	long foffset;
	FILE *file;
	char fblock;
	int requested_bytes; /* max amount of bytes to send the client this tick(client driven to avoid overflows */
	int martians; /* number of bad messages sent, used to detect a client who has went haywire */
	/*int bytes_this_sec;*/
	/*int max_bytes_sec;*/

	int room; /* players are also always in room 0 for chat?? */
}Player_t;

Player_t players[MAX_PLAYERS];



int PlayerReadByte(int p){

	if(players[p].din_count - players[p].din_pos == 0)
		return -1;
	
	return players[p].din[players[p].din_pos++];	
}



int PlayerWriteByte(int p, int d){

	if(players[p].dout_count >= sizeof(players[p].dout)-1)
		return -1;
	
	players[p].dout[players[p].dout_count++] = d;
	return 1;
}



typedef struct{

	int to; /* 0 = all players in room the sender is in, otherwise a specific player ID(if they are not in room, drop) */
	int from;
	int len;
	char buf[1024];

}Message_t;
#define MAX_MESSAGES	1024
Message_t messages[MAX_MESSAGES];



#define MAX_ROOM_PASSWORD_LEN	32
#define MAX_PLAYERS_IN_ROOM		8
typedef struct{

	char name[32+1];
	int state;
	int max_players;
	int game_id;
	int game_options[5];
	int players[MAX_PLAYERS_IN_ROOM]; /* the player in position 0 is the owner */
	int locked; /* 0 = unlocked, 1 = password lock */
	char password[MAX_ROOM_PASSWORD_LEN+1]; /* an unlocked room contains all '\0' here */
	unsigned char shared_mem[128*1024]; /* memory that all participants can share at read/write at will(coordinate for complex state) */
	int bridges[4]; /* rooms that are "bridged", we send all room data to these rooms, and receive it as well */

}Room_t;



typedef struct{

	FILE *flog;
	pthread_t thread;
	/* the main thread is the producer, the logging thread is the consumer */
	volatile unsigned char log[16*1024]; /* only the producer modifies this */
	volatile int log_in; /* only the producer modifies this */
	volatile int log_out; /* only the consumer modifies this, except when the producer resets the offsets(consumer must set log_done=1 and wait until log_done==0) */
	volatile int log_done; /* the consumer can set this to 1, the producer can set this to 0 */
}Logger_t;
Logger_t logger;

#define MAX_ROOMS	MAX_PLAYERS
#define MAX_ROOM_USERS	16
Room_t rooms[MAX_ROOMS];

#define MATCH_STATE_NONE	0
#define MATCH_STATE_SIMPLE	1

#define MAX_MATCHES		MAX_USERS
#define MAX_MATCH_PLAYERS	8


typedef struct{
	int state;
	int players[MAX_MATCH_PLAYERS];
	int rsvp[MAX_MATCH_PLAYERS];
	int ready[MAX_MATCH_PLAYERS];
	char rom_name[16];
	char password[32];
	unsigned int lfsr[8];
	int num_players, min_players, max_players;
	struct timeval rsvp_expire[MAX_MATCH_PLAYERS];
}Match_t;
Match_t matches[MAX_MATCHES];

/* room states are 8 bits/1 byte */
#define ROOM_UNUSED			0
#define ROOM_OPEN			1
#define ROOM_RUNNING			2
#define ROOM_SIMPLE_NETWORKING	128

/* commands are 8 bits/1 byte */
#define COMMAND_NONE			0 
#define COMMAND_GET_DATA		1 //(num_bytes): request 1-256 bytes from the server(pre-queued data, all transfer is controlled by the client)
#define COMMAND_DISCONNECT		2 //(void): disconnect
#define COMMAND_GET_ROOMS		3 //(void): receive a (MAX_ROOMS/8) bitmap of rooms in use
#define COMMAND_GET_ROOM_INFO		4 //(bitmap)get information about a specific room, based on a bitmap filter(for programs with simple requirements, small buffers, etc)
#define COMMAND_HOST_UNUSED_ROOM	6 //(reserve): returns 0 if no rooms are available, otherwise the id of an open room they are put in(as owner)
#define COMMAND_GET_PLAYERS		5 //(room): returns the number of players in a specific room
#define COMMAND_GET_PLAYER_ID		6 //get the player slot we are in(so we can lookup data on ourself)
#define COMMAND_GET_PLAYER_NAME	7//(id): get the name of a given player logged in
#define COMMAND_SET_PLAYER_NAME	8 //update our name, terminated by '\0'
#define COMMAND_EXTEND			9 /* provide access to future commands past the 8 bit boundary */
#define COMMAND_INVALID		10 //client has lost sync with the server, disconnect
#define COMMAND_AWAIT_ASYNCHRONOUS	11
#define COMMAND_GET_FILE_CHUNK	12
#define COMMAND_RUN_SHELL		13
#define COMMAND_FILE_STOP		14
#define COMMAND_NEXT_FILE_CHUNK	15
#define COMMAND_GET_TIMER		16
#define COMMAND_SET_TIMER		17
#define COMMAND_WHATS_MY_IP		18
#define COMMAND_PING_REQUEST		19
#define COMMAND_PING_RESPONSE		20
#define COMMAND_UNICAST		21
#define COMMAND_BROADCAST		22
#define COMMAND_SET_ROOM_PASSWORD	23
#define COMMAND_SUBSCRIBE_PLAYER	24
#define COMMAND_KICK_PLAYER		25
#define COMMAND_JOIN_ROOM		26
#define COMMAND_GET_ACTIVE_ROOMS	27
#define COMMAND_GET_FILTERED_ROOMS	28
#define COMMAND_SET_ROOM_MAX_PLAYERS	29
#define COMMAND_BREATHER		30
#define COMMAND_SET_MTU		31
#define COMMAND_FILLER_DATA		32
#define COMMAND_CHECK_MTU		33
#define COMMAND_EXCHANGE_IP		34
#define COMMAND_GET_SERVER_TIME	35
#define COMMAND_PAD_MODE_START	36
#define COMMAND_PAD_MODE_STOP		37
#define COMMAND_PAD_MODE_HISTORY	38
#define COMMAND_WRITE_SHARED_MEM	39
#define COMMAND_FLUSH_BUFFER		40
#define COMMAND_PASS			41

#define COMMAND_FAIL			255

#define SUBSCRIBE_IP_SHARE		1
#define SUBSCRIBE_SHARE_HISTORY	2
/*
#define STATE_EMPTY			0
#define STATE_IDLE			1
#define STATE_READY			2
#define STATE_PLAYING			3
#define STATE_DISCONNECTING		4
*/
#define USER_DISCONNECTED	0
#define USER_CONNECTING	1
#define USER_CONNECTED		2
#define USER_READY		8
#define USER_DISCONNECTING	128

//per player subscription options
#define SUBSCRIBE_UNICAST		1 //receive unicast data from this player
#define SUBSCRIBE_BROADCAST		2 //receive broadcast data
#define SUBSCRIBE_DISCONNECT_ALERT	4 //receive a message when they disconnect from the server(useful for detecting when to stop a game)
#define SUBSCRIBE_SHARE_DATA		64 //allow player to receive our historical data(useful for some sort of networking scheme?)
#define SUBSCRIBE_SHARE_IP		128 //allow player to receive our IP from the server(UDP hole punching, direct connection, etc.)

#define TELNET_LISTEN_PORT 23
#define GAME_LISTEN_PORT 2345

struct timeval current_time;
int server_debugging;
int server_quit;
uint64_t server_tick;
int listen_socket;
int telnet_socket;
struct sockaddr_in server_addr_in;
struct sockaddr_in telnet_addr_in;
struct sockaddr_in player_addr_in;//[MAX_PLAYERS];
//struct sockaddr_in accept_addr;
int sock_addr_len;//=sizeof(server_addr_in);
const char *telnet_greeting = "\xFF\xFB\x03\rPassword:\n12312312312312lkfjasl;kdfj;lkasjd;lfja;sldjf;lasjdfl;kjasdl;fkjal;skdjfl;asjdfl;kjasl;dkfjl;askjdfl;jasdl;kfjasl;kdjfl;kasdjf;lkasjdl;fkjasl;dfjkl;asdf";
void die(char *s, int e);
void SleepMS(int m);
void QueueByteOut(int p, unsigned char v);
unsigned char ReadByteIn(int p);
void ChangeRoom(int p, int r);
int UpdateRoom(int r);
void UpdatePlayer(int p);
void ServerLog(const char *fmt, ...);
void DebugLog(int dlevel, const char *fmt, ...);
void *LoggerThreadFunction(void *arg);
void CreateAsynchronousTask(int p, int c);
void *FileBlockReadThreadFunction();
void *FileBlockGetNextThreadFunction();
int NetworkInit();
int BytesAvailable(int p);
uint32_t Read32In(int p);
int PlayerReceive(int p);
int SocketWrite(int sock, char *buf, int len);

void DisconnectUser(int p, int now);
void UpdatePlayer(int p);
int UpdateRoom(int r);
int LoadUsers();
int FindMatch();
int JoinMatch(int p, int m);
int LeaveMatch(int p);
int IsMatchReady(int m);
int ReadyForMatch(int p);
int IsReadyForMatch(int p);
int FindRSVP(int p);
int RequestMatch(int p);
const char *common_fontset=" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]???ABCDEFGHIJKLMNOPQRSTUVWXYZ?????";
