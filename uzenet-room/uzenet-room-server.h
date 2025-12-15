#ifndef UZENET_ROOM_SERVER_H
#define UZENET_ROOM_SERVER_H

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MAX_CLIENTS 64
#define MAX_SERVICE_TUNNELS 16

// Framing control
#define FRAME_HOLD_TRANSMISSION   0xFF  // client: stop sending until further notice
#define FRAME_RESUME_TRANSMISSION 0xFE  // client: resume automatic sending

// Frame prefix mask
#define FRAME_TUNNEL_MASK         0xF0
#define FRAME_TUNNEL_PREFIX       0xF0  // upper 4 bits = 0xF

// Frame format: [0xFx][len][payload...]
#define MAX_FRAME_PAYLOAD         255

// Room commands (non-framed legacy commands)
#define ROOM_CMD_NULL             0x00
#define ROOM_CMD_PING             0x01
#define ROOM_CMD_GET_TIME         0x02
// ... Add more room commands as needed

// Service tunnel types (defined by client and server convention)
#define TUNNEL_CHAT               0
#define TUNNEL_AUDIO              1
#define TUNNEL_GAMEPLAY           2
#define TUNNEL_MATCHMAKING        3
#define TUNNEL_SIDELOAD           4
// ... Up to 15 total

// Functions to be implemented in other modules if needed
void dispatch_room_command(int client_index, uint8_t cmd);
void dispatch_tunnel_data(int client_index, int tunnel_id, const uint8_t *data, int len);

/* ────────────────────────────────────────────── */
/* Tunnel framing helpers for services           */
/* (room <-> service over AF_UNIX socket)        */
/* ────────────────────────────────────────────── */
/* Returns 0 on success, non-zero on EOF/error.
 *  - sock      : AF_UNIX socket connected to uzenet-room
 *  - out_uid   : user id for this frame
 *  - tunnel_id : which logical tunnel (0..15)
 *  - buf       : payload buffer
 *  - inout_len : on entry, size of buf; on return, actual payload length
 */
int ReadTunnelFramed(int sock,
                     uint16_t *out_uid,
                     uint8_t  *tunnel_id,
                     uint8_t  *buf,
                     uint16_t *inout_len);

/* Returns 0 on success, non-zero on error.
 *  - sock      : AF_UNIX socket to room
 *  - uid       : user id
 *  - tunnel_id : which tunnel (0..15)
 *  - buf/len   : payload to send
 */
int WriteTunnelFramed(int sock,
                      uint16_t uid,
                      uint8_t  tunnel_id,
                      const uint8_t *buf,
                      uint16_t len);

#endif // UZENET_ROOM_SERVER_H
