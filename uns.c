#include "uns.h"
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/socket.h>
#include <termios.h>
#include <netinet/in.h>
#include <errno.h>
#include <fcntl.h>



void die(char *s, int e){
	
	if(s != NULL)
		printf("%s", s);

	/* try to clean up gracefully */
	pthread_join(logger.thread, NULL); /* wait until the logger thread terminates */
	printf("Uzenet server halted with code %d", e);
	exit(e);
}



void SleepMS(int m){
	
	struct timespec ts;
	ts.tv_sec = m / 1000;
	ts.tv_nsec = (m % 1000) * 1000000;
	nanosleep(&ts, NULL);	
}



void DisconnectUser(int p){
	
	ServerLog("%s has disconnected\n", users[players[p].user].name);
	players[p].state = STATE_DISCONNECTING; /* UpdatePlayer() will handle the rest */
	players[p].counter = 500; /* we use a delay to ensure they have a chance to receive the disconnect message */
}



void QueueByteOut(int p, unsigned char v){

	players[p].dout[players[p].dout_count++] = v;
}



unsigned char ReadByteIn(int p){

	return players[p].din[players[p].din_pos++];
}



void ChangeRoom(int p, int r){

	players[p].room = r;
	/* TODO cancel existing subscriptions? */
}



void *EmulatorContainer(void *arg){
	char pbuf[512];
	FILE *f = fopen("data/bot-uers.txt", "r");
	sprintf(pbuf, "cuzebox/cuzebox \"$s\"", rom);

	
	FILE *p = popen(pbuf, "r");
	char c;
	while(1){
		c = fgetc(p);
		if(c == EOF || c == 0)
			break;
		fflush(p);
	}
	pthread_join(((EmuInst_t *)arg)->handle, NULL);
}



int LaunchEmulator(char *rom){
	int i;
	for(i = 0;i < MAX_EMULATOR;i++){
		if(emulators[i].state == EMULATOR_STOPPED){
			int perr = pthread_create(&emulators[i].handle, NULL, EmulatorContainer, (void *)(&emulators[i]));
			if(perr)
				printf("ERROR failed to create emulator thread %d\n", i);
			else
				emulators[i].state = EMULATER_RUNNING;
			return;
		}
	}
}



int UpdateRoom(int r){ /* each room is responsible for updating itself, as well as all it's players */

	int i;
	for(i = 0;i < MAX_PLAYERS_IN_ROOM;i++){
		int p = rooms[r].players[i];
		if(!p)
			continue;
		if(players[p].room != r){ /* the player doesn't think they are in this room....something has went wrong! */
		
			ServerLog("ERROR 10 - player %d(%s) is in room %d, not room %d\n", p, users[players[p].user].name, r, players[p].room);
			DisconnectUser(p);
		}
	}
	if(rooms[r].state == ROOM_UNUSED) /* must be enabled through an external event */
		return 0;

	for(i = 0;i < MAX_ROOM_USERS;i++)
		UpdatePlayer(i);
	
	return 0;
}



void UpdatePlayer(int p){

	if(players[p].state == USER_DISCONNECTED) /* must be enabled through an external event */
		return;

	int i, j, k, l, r, f, o;
	char packet_buf[2048];

	if(players[p].state != USER_DISCONNECTING){
		/* receive any network data the client sent */
		r = recv(players[p].socket, packet_buf, sizeof(packet_buf)-1, 0); /* see if any data is available */
		if(r == -1){ /* not always an actual error... */
			if(errno != EWOULDBLOCK && errno != EAGAIN){ /* is it an actual error? or just no data available? */
				players[p].state = USER_DISCONNECTED;
				printf("**ERROR** network recv() failed for client %d, errno %d\n", p, errno);
				return;
			}
		}else if(r == 0){ /* connection closed? */
			printf("client %d disconnected\n", p);
			players[p].state = USER_DISCONNECTED;
			return;
		}

		if(r > 0){ /* we did get new data? */

			if(players[p].din_count+r >= sizeof(players[p].din)){ /* did the player overflow on input(shouldn't happen, we process fast!)? */
			
				printf("%s receive buffer overflow. Server stalling?\n", users[players[p].user].name);
				DisconnectUser(p);
				return;
		}


		packet_buf[r] = '\0';
		printf("p: %d, recv[%s]\n", p, packet_buf);
		memcpy(players[p].din, packet_buf, r); /* copy the received data into the player's input pipeline */
		players[p].din_count += r;
		players[p].din_total += r;
		}
//		if(players[p].dout_count >= sizeof(players[p].dout)){ /* did the player overflow on output(client not asking for data fast enough!)? */
			
//			ServerLog("%s transmit buffer overflow. Client stalling?\n", users[players[p].user].name);
//			players[p].state = USER_DISCONNECTING;
//			return;
//		}
	}
		players[p].connection_time++;
	/* receive data and add it to din[] */
/***********************************************************************************/
if(BytesAvailable(p)){
//	printf("Have data queued[%d]\n", BytesAvailable(p));
}
	if(players[p].state == USER_CONNECTING){
printf("hacks %d\n", players[p].din_total);
		players[p].room = 0;
		players[p].sub_state = 0;

//HACK
//players[p].connected_at = current_time;
//players[p].last_activity = current_time;
//players[p].state = USER_CONNECTED;
//players[p].din_count = 0;
//players[p].din_pos = 0;
//players[p].dout_count = 0;
//players[p].dout_pos = 0;
//printf("player logged in\n");


		/* players must always indicate whether to host or join a game */
		/* they must send us exactly 16 bytes which match a user in the database to login */
		/* they are immediately returned the number of the room they are in(which simple games can discard) */
		/* the default mode of every room is ROOM_SIMPLE_NETWORKING until changed to something else */

		if(players[p].din_count < USER_KEY_LEN){ /* still waiting for login key data? */

			if(players[p].connection_time > 100000){
				DisconnectUser(p);
				return;
			}

		}else if(players[p].din_count > USER_KEY_LEN){ /* sent too much data for logging in? remove data that might be part of a command output...*/

			if(players[p].din_total > 100){ /* too much spam, not enough password... */
				printf("user sent too much data without logging in\n");
				DisconnectUser(p);
				return;				
			}

			for(i=0;i<players[p].din_count;i++){ /* remove colons, or things like "+CIPSTAMAC:" */
				if(players[p].din[i] == ':' || players[p].din[i] == '+' || players[p].din[i] == '\r' || players[p].din[i] == '\n'){
					for(j = i; j < players[p].din_count; j++)
						players[p].din[j] = players[p].din[j+1];
					players[p].din_count--;
printf("adjusting char\n");
				}
			}

			if(!strncmp(players[p].din, "CIPSTAMAC", strlen("CIPSTAMAC"))){ /* for ease of use, some impelementations may just dump the output of "AT+CIPSTAMAC?"*/
				strcpy(players[p].din, players[p].din+strlen("CIPSTAMAC"));
				players[p].din_count -= strlen("CIPSTAMAC");
				printf("adjusting string\n");
			}
printf("adjusting user data...\n");

			if(players[p].din_count > USER_KEY_LEN){ /* still too long? then something is broken */
				DisconnectUser(p);
				return;
			}
			/* TODO maintain an IP Ban List */
			//ServerLog("ERROR client at %s overflowed before logging in\n", players[p].ipv4s);
			/* TODO failed_logins++; /* keep track of failed logins, and use a cooldown timer */
			//DisconnectUser(p);

		}

		if(players[p].din_count == USER_KEY_LEN){ /* provided login key is a valid length, check against registered users in the database */

			for(i = 0;i < USER_KEY_LEN; i++) /* convert to capital */
				players[p].din[i] = toupper(players[p].din[i]);

			for(i = 0;i <= MAX_PLAYERS; i++){

				k = i;
				if(k == MAX_PLAYERS)
					break;

				for(j = 0;j < USER_KEY_LEN;j++){

					if(users[i].short_key[j] != players[p].din[j]){ /* doesn't match unencrypted key */
						k = MAX_PLAYERS;
						break;
					}

				}
				if(k != MAX_PLAYERS) /* found it? */
					break;
			}
			if(k == MAX_PLAYERS){ /* didn't find a user with that key */
				
				printf("client gave bad key\b\n");
				players[p].state = USER_DISCONNECTING;
			}else{ /*login successful! */
printf("LOGGED IN\n");
				players[p].connected_at = current_time;
				players[p].last_activity = current_time;
				players[p].state = USER_CONNECTED;
				players[p].din_count = 0;
				players[p].din_pos = 0;
				players[p].dout_count = 0;
				players[p].dout_pos = 0;
			}
		}
	}
/***********************************************************************************/
	if(players[p].state == USER_DISCONNECTING){ /* state initially set by DisconnectUser() */
printf("discoing....\n");
		if(players[p].counter){ /* if a user gives an invalid login key, keep them around for a while(perhaps slows down attacks?) */

			players[p].counter--;
			return;
		}

		unsigned char dc = COMMAND_DISCONNECT;
		SocketWrite(p, &dc, 1);
		close(players[p].socket);
		printf("player %d[%s] disconnected\n", p, users[players[p].user].name);
		players[p].state = USER_DISCONNECTED;
//////		memset((void *)players[p], 0, sizeof(players[p]));
		return;
	}
/***********************************************************************************/
//printf("STATE[%d]\n", players[p].state);
	if(players[p].state == USER_CONNECTED){
//printf("player connected state\n");
		if(players[p].dout_pos == players[p].dout_count) /* transmit(to client) buffer empty? */
			players[p].dout_pos = players[p].dout_count = 0; /* time to reset the non-circular buffer */

		if(players[p].din_pos == players[p].din_count) /* receive(from client) buffer empty? */
			players[p].din_pos = players[p].din_count = 0; /* reset it, a non-circular buffer is sufficient here and simpler/faster IMO */

		/* each player makes requests for a given length of data, at their leisure(but not so slow that dout[] overflows!) */
		/* all data received from the player is buffered, and processed as soon as possible */


		while(players[p].din_count > players[p].din_pos){ /* is there data to process? could be a message which generates data to send back */
			
			/***********************************************************************************/
			if(players[p].command == COMMAND_NONE){ /* we expect the next byte to be a command type */
				players[p].command = players[p].din[players[p].din_pos++];
				printf("P[%d] got new command[%d]\n", p, players[p].command);
			}
			/***********************************************************************************/
			if(players[p].command >= COMMAND_INVALID){ /* we received an unsupported command, assume the client has lost sync */
				printf("got bad command from player %d: %d\n", p, players[p].command);
				players[p].state = USER_DISCONNECTING;
				break;
			}

			/***********************************************************************************/
			if(players[p].command == COMMAND_BREATHER){ /* client wants a delay before more data is sent */
				
				if(BytesAvailable(p) < 1){ /* need the ticks/milliseconds to delay */
					players[p].delay++; /* since they need a delay(but we don't know how long), just delay until we know for sure to avoid disconnects. */
					break;
				}
				players[p].command = COMMAND_NONE;
				break;
			}

			/***********************************************************************************/
			if(players[p].command == COMMAND_SET_MTU){ /* client sets the maximum data size they want at once(not necessarily a real network MTU limit...) */
					
					if(BytesAvailable(p) < 2)
						break;
					players[p].mtu = players[p].din[players[p].din_pos++]<<8; /* high byte */
					players[p].mtu |= players[p].din[players[p].din_pos++]; /* low byte */
					players[p].command = COMMAND_NONE;
			}

			/***********************************************************************************/
			if(players[p].command == COMMAND_FILLER_DATA){/* the client is adding extra data, possibly to force a TCP send(Nagle's Algorithm), ignore it! */
			
				players[p].command = COMMAND_NONE;
			}

			/***********************************************************************************/
			if(players[p].command == COMMAND_CHECK_MTU){ /* the client is checking actual network MTU, using increasing/decreasing frame/packet sizes */
					
					if(BytesAvailable(p) < 1)
						break;
					
					/* the player can send any amount of non-255 bytes(ie a string of 0's works) to build out large packets(find threshold for network drops) */
					if(players[p].din[players[p].din_pos++] == 255){ /* end of data, send a response so they know that MTU works */
		
						QueueByteOut(p, COMMAND_CHECK_MTU);
						players[p].command = COMMAND_NONE;
					}
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_EXCHANGE_IP){ /* client wants to request/authorize sharing IPs with a specific player(they must subscribe to this) */

				/* this is useful for UDP hole punching for direct connections through NAT, among other things */
				/* to achieve a direct connection, see the documentation */
				if(BytesAvailable(p) < 1)
					break;

				r = players[p].din[players[p].din_pos++]; /* player we are asking about */
				if(!(players[r].subscribed[p] & SUBSCRIBE_IP_SHARE)){ /* other player not expecting this? */
					QueueByteOut(p, COMMAND_FAIL);
					break;
				}
				QueueByteOut(p, COMMAND_PASS);
				for(i = 0;i < sizeof(players[r].ipv4);i++)
					QueueByteOut(p, players[r].ipv4[i]);

				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_GET_SERVER_TIME){ /* client wants to know the current server time */
			
				if(BytesAvailable(p) < 1)
					break;

					/* 0bs0000000
						s = client specifies a string format(variable length)
						t = client specifies a time zone(TODO)
					*/
					QueueByteOut(p, 0);

				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_PAD_MODE_START){/* pad state mode, which can be overloaded into sending raw object positions, etc. */
				
				/*all client input is interpreted as data in this field, until they ask to stop */
				if(BytesAvailable(p) < 1) /* client sends ... */
					break;
				/* 0bittssspp
					i = send individually to other players, without waiting for everyone's input
					tt = leading time stamp(00 for none, 01 for 1, 10 for 2, 11 for 4)
					sss = synchronization value, commonly a PRNG seed up to full object states(000 for none, 001 for 1, 010 for 2, 011 for 4...111 for 64)
					pp = pad state bytes 1-4
				*/
				
				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/			
			if(players[p].command == COMMAND_PAD_MODE_STOP){
				
				players[p].command = COMMAND_NONE;
			}
			
			/***********************************************************************************/
			if(players[p].command == COMMAND_PAD_MODE_HISTORY){ /* client wants historical pad state data about a particular player/time. Can be used for spectators */
			
				if(BytesAvailable(p) < 6) /* need player(1), length(1), time_stamp(4)(if no data at this time, data is for nearest time after) */
					break;
				
				j = ReadByteIn(p); /* player */
				k = ReadByteIn(p); /* length */
				int time = Read32In(p);

				if(players[j].subscribed[p] & SUBSCRIBE_SHARE_HISTORY){

					//TODO FIND RELEVANT TIMESTAMP POS

					QueueByteOut(p, COMMAND_PAD_MODE_HISTORY);
					QueueByteOut(p, j);
					for(l = 0;l < 4;l++) /* copy actual time stamp(could be later than the request) */
						QueueByteOut(p, 0);
				}

				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_WRITE_SHARED_MEM){ /* client wants to write to room shared memory(game can use for anything) */
				
				if(BytesAvailable(p) < 5) /* need address(4), length(1), value(1-len) */
					break;

				int address = Read32In(p);
				unsigned char val = ReadByteIn(p);

			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_FLUSH_BUFFER){ /* client wants to discard all current data(presumably to resync) */
				players[p].din_pos = players[p].din_count = 0;
				players[p].dout_pos = players[p].dout_count = 0;
				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_GET_DATA){ /* client wants bytes from the buffer(1 or 2 byte arguments) */

				if(BytesAvailable(p) < 1)
					break;

				if(players[p].din[players[p].din_pos] == 0){ /* if 0, next byte implies multiples of 256 */
					if(BytesAvailable(p) < 2)
						break;
		//			rbytes -= 1;
				}
		//		rbytes -= 1;

				players[p].requested_bytes += players[p].din[players[p].din_pos++];
				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_DISCONNECT){
				
				players[p].state = USER_DISCONNECTING;
				players[p].command = COMMAND_NONE;
				break;
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_GET_ROOM_INFO){ /* client requesting specific room info */

				if(BytesAvailable(p) < 1) /* need room ID */
					break;

				r = ReadByteIn(p); /* room ID */ 
				for(i = 0;i < sizeof(rooms[r].name); i++) /* all room names are '\0' padded to max length */
					QueueByteOut(p, rooms[r].name[i]);

				for(i = 0;i < MAX_PLAYERS_IN_ROOM; i++)
					QueueByteOut(p, rooms[r].players[i]); /* empty player slots are padded with 0 */

				QueueByteOut(p, rooms[r].game_id);
				QueueByteOut(p, rooms[r].locked);
				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_HOST_UNUSED_ROOM){
				
				if(BytesAvailable(p) < MAX_ROOM_PASSWORD_LEN) /* password(MAX_ROOM_PASSWORD_LEN...all '\0' for an open room) */
					break;

				r = 0; /* if we return 0, it means there are no open rooms(room 0 is a default/reserved room with empty password and no owner) */
				/* the client musif they want to reserve the room), and MAX_ROOM_PASSWORD_LEN bytes */
				for(i = 1;i < MAX_ROOMS;i++){
					for(j = 0;j < MAX_PLAYERS_IN_ROOM;j++){
						if(rooms[i].players[j]){/* not empty, we search entire list incase a player left and the array isn't sorted for an owner */
							r = 0;
							break;
						}
						r = i;
					}
					if(r)
						break;
				}
				
				//if(r == 0).../* shouldn't happen, we have enough room for each player to have their own */
				ChangeRoom(p, r); /* since no one else is in, they will automatically become owner */
				for(i = 0;i < MAX_ROOM_PASSWORD_LEN;i++)
					rooms[r].password[i] = ReadByteIn(p);
				
				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_JOIN_ROOM){ /* must specify room number, and password(default is no password, ie. buffer is all '\0' */
				
				if(BytesAvailable(p) < 1+MAX_ROOM_PASSWORD_LEN) /* need room, and a password(always full length, padded '\0' if necessary, even for "open" rooms) */
					break;
				
				j = ReadByteIn(p);
				if(j < 0 || j >= MAX_ROOMS){
					DisconnectUser(p);
					break;
				}

				unsigned char pbuf[MAX_ROOM_PASSWORD_LEN+1];
				for(i = 0;i < MAX_ROOM_PASSWORD_LEN;i++)
					pbuf[i] = ReadByteIn(p);

				if(strcmp(rooms[j].password, pbuf)){
					QueueByteOut(p, COMMAND_FAIL);
					break;
				} 
				ChangeRoom(p, j);
				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_GET_ACTIVE_ROOMS){ /* client wants to know all active rooms without filtering */

				for(i = 0;i < MAX_ROOMS;i++){ /* this is fixed length to make simpler client side code, with negligible performance cost during non-play */
					if(i == 0 || rooms[i].players[0]) /* default room, or a room with an owner, otherwise it's not active*/
						QueueByteOut(p, 1);
					else
						QueueByteOut(p, 0);	
				}

				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_GET_FILTERED_ROOMS){ /* get up to x rooms that match the filter, for more advanced client side code */

				if(BytesAvailable(p) < 8+6)
					break;

				/* filters, 8 bytes:
						game_id(0 is any),
						min_ping(in multiples of 10, of any player),
						max_ping(in multiples of 10, of any player),
						game_options[5](xx,xx,xx,xx,xx=literal option matches..0,0,0,0,0 is any options...ff,ff,ff,ff,ff is any options not 0),
				*/
				/* sorts, 6 bytes:
						byte 0: 0=no sort..1=low to hi ping sort..2=hi to low ping sort
						byte 1: 0=no sort..1=low to high game_options[0] sort..2=hi to low game_options[0] sort
						byte 2-5: ...game_options[1..3]
						byte 5:0=no sort..1=low to high game_options[4] sort..1=hi to low game_options[0] sort		
				*/
			//	rbytes -= 12;

				int rbuf[MAX_ROOMS];
				int omit;
				unsigned char f_game_id = ReadByteIn(p);
				unsigned char f_min_ping = ReadByteIn(p);
				unsigned char f_max_ping = ReadByteIn(p);
				unsigned char f_game_options[5];
				for(i = 0;i < sizeof(f_game_options);i++)
					f_game_options[i] = ReadByteIn(p);

				for(i = 1;i < MAX_ROOMS;i++){
					omit = 0;
		//			rbuf[rbo] = 0;

					if(f_game_id && f_game_id != rooms[i].game_id)
						omit = 1;
					
					for(j = 0;j < MAX_ROOM_USERS;j++){
						if(f_min_ping && f_min_ping < players[rooms[i].players[j]].ping)
							omit = 1;
						if(f_max_ping && f_max_ping > players[rooms[i].players[j]].ping)
							omit = 1;
					}

					for(j = 0;j < sizeof(f_game_options);j++){
						if(f_game_options[j] == 0) /* match any option */
							continue;
						if(f_game_options[j] == 255 && rooms[i].game_options[j] != 0) /* match any option not 0 */
							continue;
						omit = 1;
					}
	//				if(!omit)
	//					rbuf[rbo] = i; /* this room passes the filters */
	//				rbo++;
				}
				
				/* now sort by the metrics specified for each game options */
				for(i = 0;i < sizeof(f_game_options);i++){
	//				do{
	//					j = 0;
	//					for(k = 1;k < rbo;k++){
	//						if(f_game_options[i] == 2 && rbuf[k-1] < rbuf[k]){ /* sort max to min */
	//							unsigned char rt = rbuf[k-1];
	//							rbuf[k-1] = rbuf[k];
	//							rbuf[k] = rt;
	//							j = 255; /* keep sorting */
	//						}else if(f_game_options[i] == 1 && rbuf[k] < rbuf[k-1]){ /* sort min to max */
	//							unsigned char rt = rbuf[k];
	//							rbuf[k] = rbuf[k-1];
	//							rbuf[k-1] = rt;
	//							j = 255; /* keep sorting */
	//						}
	//					}
	//				}while(j == 255);
				}
				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_KICK_PLAYER){ /* allow a room owner to kick a player out of the room */

				if(rooms[players[p].room].players[0] != p){ /* client attempting to kick from a room they don't own? */
					DisconnectUser(p);
					break;
				}

				if(BytesAvailable(p) < 1) /* need the ID of the player to kick */
					break;
		//		rbytes -= 1;

				j = players[p].din[players[p].din_pos++]; /* player to kick */

				/* TODO handle any race conditions for player leaving the room before this is processed... */
				for(i = 1;i < MAX_ROOM_USERS;i++){ /* can't kick the owner */
					if(rooms[players[p].room].players[i] == j){
						QueueByteOut(j, COMMAND_KICK_PLAYER); /* let the client know */
						ChangeRoom(j, 0);
						break;
					}
				}
				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_SET_ROOM_MAX_PLAYERS){

				if(rooms[players[p].room].players[0] != p){ /* client attempting to change room they don't own? */
					DisconnectUser(p);
					break;
				}
				
				if(BytesAvailable(p) < 1) /* need max players */
					break;
		//		rbytes -= 1;

				rooms[players[p].room].max_players = players[p].din[players[p].din_pos++];

				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_SET_ROOM_PASSWORD){

				if(rooms[players[p].room].players[0] != p){ /* client attempting to change room they don't own? */
					DisconnectUser(p);
					break;
				}
				
				if(BytesAvailable(p) < MAX_ROOM_PASSWORD_LEN) /* regardless of the actual password, must be padded out with '\0' */
					break;
		//		rbytes -= MAX_ROOM_PASSWORD_LEN;
				
				for(i = 0;i < MAX_ROOM_PASSWORD_LEN;i++)
					rooms[players[p].room].password[i] = players[p].din[players[p].din_pos++];

				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_SUBSCRIBE_PLAYER){ /* allow/disallow data from another player */

				if(BytesAvailable(p) < 2) /* need player, and subscription type */
					break;
		//		rbytes -= 2;
				
				k = players[p].din[players[p].din_pos++]; /* the player in question */
				/* 0 = ubsubscribe(stop receiving data from this player) */
				/* 1 = subscribe until room leave */
				/* > 1 = stay subscribed after room leave */

				players[p].subscribed[k] = players[p].din[players[p].din_pos++];
				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_BROADCAST){ /* send a data chunk to all players in the room(they may not be subscribed) */
				
				j = players[p].din[players[p].din_pos]; /* get length of data */
				if(BytesAvailable(p) < 1+j) /* not all data is ready, try again next tick */
					break;
				
				players[p].din_pos++;
				for(i = 0;i < MAX_ROOM_USERS;i++){
					k = rooms[players[p].room].players[i];
					if(!players[k].subscribed[p]) /* are they subscribed to this player? */
						continue;
					for(l = 0;l < j;l++)
						QueueByteOut(k, players[p].din[players[p].din_pos+l]);
				}
				players[p].din_pos += j;
				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/	
			if(players[p].command == COMMAND_UNICAST){ /* send a data chunk to a specific player(they may not be subscribed) */
				
				j = players[p].din[players[p].din_pos]; /* get length of data */
				if(BytesAvailable(p) < 1+j)
					break;
				players[p].din_pos++;

				k = rooms[players[p].room].players[i];
				if(players[k].subscribed[p]){ /* are they subscribed to this player? */
					for(l = 0;l < j;l++)
						QueueByteOut(k, players[p].din[players[p].din_pos++]);
				}

				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_PING_REQUEST){
				
				if(BytesAvailable(p) < 1) /* need a ping response number */
					break;
		//		rbytes -= 1;

				QueueByteOut(p, COMMAND_PING_RESPONSE);
				QueueByteOut(p, players[p].din[players[p].din_pos++]);
				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_PING_RESPONSE){
				
				if(BytesAvailable(p) < 1) /* need a ping response number */
					break;
		//		rbytes -= 1;

	//			players[p].last_ping_rx_time = server_tick;
	//			if(!players[p].last_ping_tx_time)
	//				players[p].last_ping_tx_time = server_tick;
				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_WHATS_MY_IP){ /* asking the server what their external(public, not internal NAT)IPv4 address is */
			
				for(i = 0;i < 4;i++)
					QueueByteOut(p, players[p].ipv4[i]);

				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_SET_TIMER){ /* asking server to set a timer */
 
				if(BytesAvailable(p) < 9) /* need index(1), value(4), reset/state(4, 0 if single shot) */
					break;
			
		//		rbytes -= 9;
				j = players[p].din[players[p].din_pos++]; /* the timer to set */
				if(j < 0 || j > sizeof(players[p].timer)){ /* assume the client has lost it's mind, or is malicious... */
					DisconnectUser(p);
					break;
				}
				
				players[p].timer[j] = 0;
				for(i = 0;i < 4;i++){
					players[p].timer[j] <<= 8;
					players[p].timer[j] += players[p].din[players[p].din_pos++];
				}

				players[p].timer_state[j] = 0;
				for(i = 0;i < 4;i++){
					players[p].timer_state[j] <<= 8;
					players[p].timer_state[j] += players[p].din[players[p].din_pos++];
				}

				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_GET_TIMER){ /* asking server for a timer status */
 
				if(BytesAvailable(p) < 1) /* need timer number */
					break;

				j = ReadByteIn(p); /* the timer to get */
				if(j < 0 || j > sizeof(players[p].timer)){ /* assume the client has lost it's mind, or is malicious... */
					DisconnectUser(p);
					break;
				}

		//		Queue32Out(p, players[p].timer[j]);
				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_GET_FILE_CHUNK){ /* client wants part of a file(open it if needed) */
				
				/* example buffer ['d','i','r','.','t','x','t','\0',0,0,0,2,255] */
				j = 255;
				char fname[64];
			
				for(i = 0;i <= 32;i++){ /* check for a terminated filename string */
					if(BytesAvailable(p) < i+1)
						break;
					fname[i] = players[p].din[players[p].din_pos+i];
					if(fname[i] == '\0')
						j = i+1;
				}

				if(j < 2){
					DisconnectUser(p); /* client has lost it's mind? */
					break;
				}

				if(j == 255)
					break;

				if(BytesAvailable(p) < j+1+(3+2)) /* haven't got the complete request yet, try again next tick */
					break;
				
				players[p].din_pos += j+1+3+2;

				CreateAsynchronousTask(p, COMMAND_GET_FILE_CHUNK); /* we don't just read files inline, as they can cause stalls for all clients */
				players[p].command = COMMAND_AWAIT_ASYNCHRONOUS;
				break; /* this will be caught at the top so the client can't do anything until the thread is done */
			}
			/***********************************************************************************/
			if(players[p].command == COMMAND_NEXT_FILE_CHUNK){ /* asking the server for the next chunk of an already open file */
			


				players[p].command = COMMAND_NONE;
			}
			/***********************************************************************************/
		}/* while(players[p].din_count > players[p].din_pos) */

		/* done processing received data, check for data we should send */
		if(players[p].requested_bytes && players[p].dout_pos < players[p].dout_count){
			uint8_t pbuf[sizeof(players[p].dout)+1];
			for(i = 0;i < players[p].requested_bytes && players[p].dout_pos <= players[p].dout_count;i++){
				if(i == players[p].mtu)
					break;
				pbuf[i] = players[p].dout[players[p].dout_pos++];
				players[p].requested_bytes--;
			}
			SocketWrite(players[p].socket, pbuf, i);
		}
	}/* if(players[p].state == USER_CONNECTED) */
if(players[p].state == 3){while(1);}
/***********************************************************************************/

}



void ServerLog(const char *fmt, ...){

//	if(logger.log_done == 1){ /* consumer has indicated it has written all data, and is waiting for us to reset this */
//		if(logger.log_in != logger.log_out){ /* consumer didn't notice data we wrote "at the same time" they detected no data, continue consumption */
//			logger.log_done = 0; /* tell the consumer to continue */
//asm volatile("": : :"memory"); /* memory fence */	
//		}else{ /* consumer has eaten all the data and is waiting, safe for us to reset the offsets(this must happen eventuallly, or buffer overflow */
//			logger.log_in = logger.log_out = 0;
//			logger.log[logger.log_in] = '\0';
//asm volatile("": : :"memory"); /* memory fence */
//			logger.log_done = 0; /* tell the consumer to continue */
//		}
//	}
	
//	va_list args;
//	va_start(args, fmt);
//	int c = vsnprintf((char *)&logger.log+logger.log_in, sizeof(logger.log)-(logger.log_in+64), fmt, args);
//	if(c < 0 || c >= sizeof(logger.log)-(logger.log_in+64)){ /* logging has failed */
//		va_end(args);
//		die("ERROR ServerLog()->vsnprintf() logging has failed\n", 10); /* something is fundamentally wrong...stop the server */
//	}
//asm volatile("": : :"memory"); /* memory fence */
//	logger.log_in += c;
//	va_end(args);
}



void DebugLog(int dlevel, const char *fmt, ...){

	if(server_debugging < dlevel) /* this message isn't logged at this debugging level */
		return;

	va_list args;
	va_start(args, fmt);
	ServerLog(fmt, args);
	va_end(args);
}



void *LoggerThreadFunction(void *arg){ /* logging to file is handled asynchronously(is this required?), because timing delays in the main thread would be unacceptable */

//	DebugLog(1, "Logging started, waiting 1250Ms\n");
	/* rapidly closing then starting the server could overwrite a log name in the same second... */
//	DelayMS(1250);
//	char fname[128];
//	sprintf(fname, "logs/%ld.txt", current_time.tv_sec);
//	logger->flog = fopen(fname, "w+");
		
//	if(logger->flog == NULL){			
//		printf("ERROR can't create logs/active_log.txt");
//		log_done = 255; /* indicate to the main thread that there was an error */
//		return; /* nothing more we can do */
//	}

//	logger->log_done = 1; /* indicate to the main thread that we have made the first file write */
//	DebugLog(1, "Debug file is %s\n", fname);
	
//	while(1){
//		DelayMS(1);

//		if(logger->log_done) /* the producer has stopped logging */
//			continue;

//		if(logger->log_in == logger->log_out){ /* nothing to write, ask the producer to synchronize/reset the buffer */
//			logger->log_done = 1;
//			while(logger->log_done == 1); /* wait for the producer to reset this(the producer will presumably set log_in=log_out=0 */
//			continue;
//		}
		
//		if(logger->log_out >= logger->log_out)
//			continue;
		/* else there is something to write */
		
//		int i;
//		for(i = logger.log_out;i < logger.log_in;i++){
//			int r = fputc(logger.log[i], logger->flog);
			
//			if(r == -1){
//				printf("ERROR logger failed to write[log_in=%d, log_out=%d, log[log_out]=%d\n", logger.log_in, logger.log_out, logger.log[logger.log_out]);
//				return; /* nothing more we can do */
				
//asm volatile("": : :"memory"); /* memory fence */
//				logger.log[logger.log_out++] = '\0';
//			}
//		}
//	}
}


void CreateAsynchronousTask(int p, int c){ /* this assumes an argument has already been placed in players[p].temp[] */

	players[p].command = COMMAND_AWAIT_ASYNCHRONOUS; /* eliminate race conditions */
asm volatile("": : :"memory"); /* memory fence */
	int perr = 0;
/*
	if(c == COMMAND_GET_FILE_CHUNK)
		perr = pthread_create(&players[p].async_task_thread, NULL, FileBlockReadThreadFunction, NULL);
	else if(c == COMMAND_RUN_SHELL)
		perr = pthread_create(&players[p].async_task_thread, NULL, ShellRedirectThreadFunction, NULL);
	else
		perr = 255;
*/	
	if(perr)
		DisconnectUser(p); /* something went wrong which shouldn't have, assume the server is under attack */
}



void *FileBlockReadThreadFunction(){ /* *one shot, keeps file open*read a specific file chunk, opening the file if necessary, and closing an old one before that if required */
int p = 0;
	if(strcmp(players[p].filename, players[p].temp)){ /* need to open a different file? */
		if(players[p].file != NULL){ /* close existing */
			fclose(players[p].file);
			players[p].file = NULL; /* mark closed */
			players[p].filename[0] = '\0'; /* mark closed */
		}
	}

	if(players[p].file == NULL){ /* need to open a new file */
		players[p].file = fopen(players[p].temp, "r");
		if(players[p].file == NULL){
			players[p].filename[0] = '\0'; /* mark closed */
			QueueByteOut(p, COMMAND_FILE_STOP);
			QueueByteOut(p, 0); /* error, can't open */
		}
	}

	char buf[(64*1024)+1+1]; /* larger than the largest chunk a client can request */
	fseek(players[p].file, players[p].foffset, SEEK_SET);
	int bytes = fread(buf, 1, players[p].fblock, players[p].file); /* if this is the last block, we could get less bytes than the block size */

	while(bytes < players[p].fblock)
		buf[bytes++] = '\0'; /* we pad out the end block for simplicity of state. The client should request filesize beforehand if needed */

asm volatile("": : :"memory"); /* memory fence */
	while(!players[p].waiting_thread); /* the main thread writes 1 to this, then yields control of client data/buffers/state until this thread writes 0 */
asm volatile("": : :"memory"); /* memory fence */

	/* we know the main thread wont do anything until we relinquish control(not even send queued output data) */
	/* this means we can leave the existing buffer as is, and send it directly to avoid complexity */
	/* we send the data in one shot. If this is too fast for the client, they shouldn't have asked for such a large block */
	SocketWrite(players[p].socket, buf, bytes);
	
	goto FBRTF_DONE;

FBRTF_DONE:
asm volatile("": : :"memory"); /* memory fence */
	players[p].waiting_thread = 0;
asm volatile("": : :"memory"); /* memory fence */
}


void *FileBlockGetNextThreadFunction(){ /* this assumes an open file, and we send from wherever the file offset was last time */

int p = 0;
asm volatile("": : :"memory"); /* memory fence */
	while(!players[p].waiting_thread); /* the main thread writes 1 to this, then yields control of client data/buffers/state until this thread writes 0 */
asm volatile("": : :"memory"); /* memory fence */

	if(players[p].file == NULL){
		DisconnectUser(p); /* the client is messing up... */
		goto FBGNTF_DONE;
	}

	char buf[(64*1024)+1+1]; /* larger than the largest chunk a client can request */
	fseek(players[p].file, players[p].foffset, SEEK_SET);
	int bytes = fread(buf, 1, players[p].fblock, players[p].file); /* if this is the last block, we could get less bytes than the block size */

	while(bytes < players[p].fblock)
		buf[bytes++] = '\0'; /* we pad out the end block for simplicity of state. The client should request filesize beforehand if needed */	

	SocketWrite(players[p].socket, buf, bytes);
	
	goto FBGNTF_DONE;

FBGNTF_DONE:

asm volatile("": : :"memory"); /* memory fence */
	players[p].waiting_thread = 0;
asm volatile("": : :"memory"); /* memory fence */		
}




int main(int argc, char *argv[]){

	printf("%s", banner);
	int opt;

	while((opt = getopt(argc, argv, "dlwh")) != -1){
		switch (opt){
			//case 'd': server_debugging = 1; break;
			//case 'l': mode = LINE_MODE; break;
			//case 'w': mode = WORD_MODE; break;
			//case 'h': printf("Usage: %s [-d, enable debug] [-e, enable line mode] [-w, enable word mode] [-h, this menu]\n"); break;
			default:
				printf("\nunknown argument %c\n", opt);
				printf("Usage:\n");
				die("Uzenet server halted\n", 1);
		}
	}


	if(LoadUsers())
		printf("WARNING: failed to load any users from data/users.dat\n");

	int perr = pthread_create(&logger.thread, NULL, LoggerThreadFunction, NULL); /* initialize logging */

	rooms[0].state = ROOM_OPEN;
	int i,j,k;

	if(NetworkInit())
		die("ERROR NetworkInit() failed\n", 1);

	ServerLog("Uzenet server initialized\n");
	printf("Uzenet Server initialized\n");

	gettimeofday(&current_time, NULL);
	srand(current_time.tv_sec);

	while(!server_quit){
		gettimeofday(&current_time, NULL);

//		if(log_done == 255) /* logging thread stopped */
//			die("ERROR logging thread has halted\n", 11);
			
		for(i = 0;i < MAX_ROOMS;i++){
			int r = UpdateRoom(i); /* each room updates all players inside it */				
			if(r){
				ServerLog("ERROR failed to update room %d\n", i);
				printf("ERROR failed to update room %d\n", i);
				server_quit = 1;
				break;
			}
		}
		
		if(server_quit)
			break;

		if(1){
			int p = MAX_PLAYERS+1;
			int i;
			
			for(i = 0;i < MAX_PLAYERS;i++){
					if(players[i].state == STATE_EMPTY){
						p = i;
						break;
					}
			}
			
			if(p >= MAX_PLAYERS)
				continue;

			players[p].socket = accept(listen_socket, (struct sockaddr*)NULL, NULL);//(struct sockaddr *)&client_addrs[c], &client_addr_sizes[c]);
			if(players[p].socket == -1){
				
				if(errno != EWOULDBLOCK && errno != EAGAIN){

					printf("**ERROR** network accept() failed, errno %d\n", errno);
					break;
				}
				players[p].socket = 0;
				continue;
			}
printf("accepted client\n");
			fcntl(players[p].socket, F_SETFL, O_NONBLOCK);//inherited from listen_socket, not needed ever??
players[p].state = USER_CONNECTING;

		//	players[p].state = USER_CONNECTED;
		//	players[p].mtu = 32; /* maximum packet size to send(additional to the requested bytes functionality) */
		//	players[p].command == COMMAND_NONE;
			players[p].din_pos = players[p].dout_pos = players[p].din_count = players[p].dout_count = players[p].din_total = 0;
			for(i = 0;i < USER_KEY_LEN;i++) /* ask the user to take this number through a convulated lookup into their stored key */
				players[p].challenge[i] = rand()%USER_KEY_LEN; /* simple games can ignore and provide their verbatum key as stored(less secure...I guess) */
			SocketWrite(players[p].socket, players[p].challenge, USER_KEY_LEN);
		}
	}

	die("Uzenet server halted\n", 0);
}


int BytesAvailable(int p){
	return (players[p].din_count-players[p].din_pos);
}

uint32_t Read32In(int p){
	return 0;
}


int SocketWrite(int sock, char *buf, int len){
	return send(sock, buf, len, 0);
}

int NetworkInit(){

	server_addr_in.sin_family = AF_INET;//using IPv4 TCP
	server_addr_in.sin_port = htons(LISTEN_PORT);
	server_addr_in.sin_addr.s_addr = INADDR_ANY;//use whatever interface the OS thinks

	errno = 0;	
	if((listen_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		//failed to create the listen socket for incoming connections
		printf("**ERROR** failed to create socket, errno %d\n", errno);
		return -1;
	}
	fcntl(listen_socket, F_SETFL, O_NONBLOCK);//make it non-blocking, so we can poll for data
	//need to set this, so that rapid fire close->bind loops work??
	int enable = 1;
	int ssoe = setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, (void *)&enable, sizeof(enable));//allow addr reuse
	if(ssoe){
		printf("\e[31;3mCan't setup reuse address option, errno %d\n\e[0m", errno);
		return -1;
	}
	ssoe = setsockopt(listen_socket, SOL_SOCKET, SO_REUSEPORT, (void *)&enable, sizeof(enable));//allow port reuse
	if(ssoe){
		printf("\e[31;3mCan't setup reuse port option, errno %d\n\e[0m", errno);
		return -1;
	}

	if(bind(listen_socket, (struct sockaddr *)&server_addr_in, sizeof(server_addr_in)) == -1){
		//failed to bind the address to the listen socket

		printf("\e[31;3mnetwork bind() failed\n\e[0m");
		return -1;
	}

	if(listen(listen_socket, 10) == -1){		
		printf("\e[31;3msocketlisten() failed\n\e[0m");
		return -1;
	}
	return 0;
}

int LoadUsers(){

	int u;
	for(u = 0; u < MAX_USERS; u++)
		users[u].name[0] = '\0';

	FILE *f = fopen("data/users.dat", "r");
	if(f == NULL){ /* try to create it */
		f = fopen("data/users.dat", "w");
		if(f != NULL)
			fclose(f);
		return 1;
	}

	char buf[1024];
	u = 0;
	while(fgets(buf, sizeof(buf), f)){ /* parse each line */
		if(u == MAX_USERS){
			printf("WARNING: over maximum users in user fiile\n");
			return 0;
		}

		if(buf[strspn(buf, " ")] == '\n') /* accept blank lines */
			continue;
		if(buf[0] == '#') /* accept comment lines */
			continue;


		sscanf(buf, " %s , %s , %s , %d , %d , %d , %d , %s , %s , ", users[u].short_name, users[u].name, users[u].country, users[u].time_zone, users[u].reservation_room, users[u].reservation_expire.tv_sec, users[u].join_date.tv_sec, users[u].short_key, users[u].long_key);
		printf("Loaded user:\n");
		printf("\t%s\n", users[u].short_name);
		printf("\t%s\n", users[u].name);
		printf("\t%s\n", users[u].country);
		printf("\t%d\n", users[u].time_zone);
		printf("\t%d\n", users[u].reservation_room);
		printf("\t%d\n", users[u].reservation_expire.tv_sec);
		printf("\t%d\n", users[u].join_date.tv_sec);
		printf("\t%s\n", users[u].short_key);
		printf("\t%s\n", users[u].long_key);
	}
	return u;
}

int SaveUsers(){
	
	FILE *f = fopen("data/users.dat", "w");
	if(f == NULL){
		printf("ERROR: failed to save user file\n");
		return 1;
	}
	int u;
	for(u = 0; u < MAX_USERS; u++){
		if(users[u].name[0] == '\0')
			break;
printf("saved line: %s , %s , %s , %d , %d , %d , %d , %s , %s ,\n", users[u].short_name, users[u].name, users[u].country, users[u].time_zone, users[u].reservation_room, users[u].reservation_expire.tv_sec, users[u].join_date.tv_sec, users[u].short_key, users[u].long_key);
		fprintf(f, "%s , %s , %s , %d , %d , %d , %d , %s , %s ,", users[u].short_name, users[u].name, users[u].country, users[u].time_zone, users[u].reservation_room, users[u].reservation_expire.tv_sec, users[u].join_date.tv_sec, users[u].short_key, users[u].long_key);
		//fprintf(f, "\n");
	}
	fclose(f);
}

int AddUser(char *sn, char *nm, char *cy, int tz, int jd, char *sk, char *lk){
//TODO DONT ALLOW DUPLICATES
	int u;
	for(u = 0; u <= MAX_USERS; u++){

		if(u == MAX_USERS){
			printf("WARNING: user count is at maximum\n");
			return -1;
		}
		if(users[u].name[0] != '\0'){
			printf("[%c]", users[u].name[0]);
			continue;
		}else
			break;
	}

	if(jd == 0){
		struct timeval tv;
		gettimeofday(&tv, NULL);
		jd = tv.tv_sec;
	}

	strcpy(users[u].short_name, sn);
	strcpy(users[u].name, nm);
	strcpy(users[u].country, cy);
	users[u].time_zone = tz;
	users[u].join_date.tv_sec = jd;
	strcpy(users[u].short_key, sk);
	strcpy(users[u].long_key, lk);

	printf("Added user [%s]\n", users[u].name);
	SaveUsers();

	return u;
}
