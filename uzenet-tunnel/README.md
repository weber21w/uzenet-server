# uzenet-tunnel

`uzenet-tunnel` is a tiny, shared framing layer used between **uzenet-room** and all
**UzeNet services** over Unix domain sockets.

It provides a single, consistent way to:

- Deliver a **logged-in user identity** to a service.
- Exchange **framed messages** with length-prefixing and type/flags.
- Hide all the annoying details of partial `read()` / `write()` calls.

This lets services like `uzenet-lichess`, `uzenet-virtual-fujinet`, and
`uzenet-radio` all share the same “tunnel” abstraction, even though their
payloads are completely different.

---

## Directory layout

Typical tree:

```text
uzenet-room/
uzenet-tunnel/
    uzenet-tunnel.h
    uzenet-tunnel.c
uzenet-lichess/
uzenet-radio/
uzenet-virtual-fujinet/
uzenet-ssh/
...
```

Each service that talks to `uzenet-room` over an AF_UNIX socket should:

```c
#include "../uzenet-tunnel/uzenet-tunnel.h"
```

and link against `../uzenet-tunnel/uzenet-tunnel.c`.

---

## Design goals

- **One framing format** for all room↔service traffic.
- **Simple service code**: blocking helpers that either complete or fail.
- **Explicit login**: a service always knows which UzeNet user it is serving.
- **Service‑specific payloads**: no global assumptions beyond `type/flags/len`.

This is *not* the same as the client protocol (`0xF0 | tunnel` frames between
Uzebox and `uzenet-room`). `uzenet-tunnel` is only for **room ↔ service**.

---

## Wire format

All frames between `uzenet-room` and a service look like this:

```text
+--------+--------+-----------+-----------+-------------------+
| type   | flags  | len_hi    | len_lo    | payload[len]      |
+--------+--------+-----------+-----------+-------------------+
  1 byte  1 byte    1 byte      1 byte        len bytes
```

Where:

- `type`  – message type (shared across all services).
- `flags` – free for service / room use (bitfield).
- `len`   – big‑endian 16‑bit payload length (0–65535).

Services do not need to manually parse this header; `utun_read_frame()` and
`utun_write_frame()` deal with it.

### Standard frame types

`uzenet-tunnel.h` defines a small global set of types:

```c
#define UTUN_TYPE_LOGIN  0x01  /* room -> service, first frame on a new conn */
#define UTUN_TYPE_DATA   0x02  /* service-specific payloads */
#define UTUN_TYPE_PING   0x03  /* optional, room <-> service */
#define UTUN_TYPE_PONG   0x04  /* optional, room <-> service */
```

You can define more if needed, but these four cover the normal cases.

### LOGIN meta payload

The first frame room sends on a new AF_UNIX connection is always:

- `type  = UTUN_TYPE_LOGIN`
- `flags = 0` (currently unused)
- `payload` = login metadata

Canonical C view of the login payload:

```c
struct UtunLoginMeta{
    uint16_t user_id;   /* big-endian on the wire */
    uint16_t reserved;  /* future use (service id / flags / etc.) */
};
```

On the wire:

```text
+---------+---------+---------+---------+
| user_hi | user_lo | res_hi  | res_lo  |
+---------+---------+---------+---------+
```

Each service typically:

1. Reads one LOGIN frame.
2. Stores `user_id` into its per-client state (`client_t`).
3. Optionally pre-loads user prefs (tokens, ACLs, etc.).
4. Treats all subsequent `UTUN_TYPE_DATA` frames as requests from that user.

---

## C API (uzenet-tunnel.h)

```c
#ifndef UZENET_TUNNEL_H
#define UZENET_TUNNEL_H

#include <stdint.h>
#include <stddef.h>

#define UTUN_MAX_PAYLOAD 256

#define UTUN_TYPE_LOGIN  0x01
#define UTUN_TYPE_DATA   0x02
#define UTUN_TYPE_PING   0x03
#define UTUN_TYPE_PONG   0x04

typedef struct{
    uint8_t  type;
    uint8_t  flags;
    uint16_t length;    /* host-endian */
    uint8_t  data[UTUN_MAX_PAYLOAD];
} TunnelFrame;

/* Blocking helpers: either complete or fail.
 * Return value:
 *   utun_read_full:
 *     1  success (exact len bytes read)
 *     0  EOF
 *    -1  error (errno set)
 *
 *   utun_write_full:
 *     0  success
 *    -1  error (errno set)
 */
int utun_read_full(int fd, void *buf, size_t len);
int utun_write_full(int fd, const void *buf, size_t len);

/* Frame helpers.
 * Return value:
 *   utun_read_frame:
 *     1  success (frame is in *fr)
 *     0  EOF
 *    -1  error (invalid length or I/O error)
 *
 *   utun_write_frame:
 *     0  success
 *    -1  error (I/O error)
 */
int utun_read_frame(int fd, TunnelFrame *fr);
int utun_write_frame(int fd, const TunnelFrame *fr);

#endif
```

### Implementation notes

`uzenet-tunnel.c` implements these functions and handles:

- Partial reads/writes via `utun_read_full` / `utun_write_full`.
- Big‑endian `len` encoding.
- Over‑length frames (drains payload into a temporary buffer and returns error).

A service never has to worry about getting “half a frame” — it always sees a
complete `TunnelFrame`.

---

## Typical service usage pattern

### Accept and login

Service creates a Unix socket (e.g. `/run/uzenet/lichess.sock`), then:

```c
#include "../uzenet-tunnel/uzenet-tunnel.h"

typedef struct{
    int      fd;
    uint16_t user_id;
    /* more per-client state... */
} client_t;

static void handle_client(int fd){
    TunnelFrame fr;
    client_t c;

    memset(&c, 0, sizeof(c));
    c.fd = fd;
    c.user_id = 0xffff;

    /* 1) Expect a LOGIN frame from room */
    int rc = utun_read_frame(fd, &fr);
    if(rc <= 0) {
        close(fd);
        return;
    }

    if(fr.type == UTUN_TYPE_LOGIN && fr.length >= 2){
        uint16_t uid = (uint16_t)((fr.data[0] << 8) | fr.data[1]);
        c.user_id = uid;
        /* optional: use fr.data[2..3] as service flags, etc. */
    }else{
        /* unexpected, drop connection */
        close(fd);
        return;
    }

    /* 2) Main loop: DATA frames from room */
    while(1){
        rc = utun_read_frame(fd, &fr);
        if(rc == 0){
            /* EOF */
            break;
        }else if(rc < 0){
            /* error */
            break;
        }

        if(fr.type == UTUN_TYPE_DATA){
            /* interpret fr.data[0..fr.length-1] as service-specific payload */
            handle_service_payload(&c, fr.data, fr.length);
        }else if(fr.type == UTUN_TYPE_PING){
            /* optional: reply with PONG */
            TunnelFrame pong = {0};
            pong.type   = UTUN_TYPE_PONG;
            pong.flags  = 0;
            pong.length = 0;
            utun_write_frame(fd, &pong);
        }else{
            /* ignore unknown types for now */
        }
    }

    close(fd);
}
```

### Sending data back to the client

When the service wants to send a message back to the Uzebox:

```c
static int send_service_msg(client_t *c, const void *msg, uint16_t len){
    TunnelFrame fr;

    if(len > UTUN_MAX_PAYLOAD)
        len = UTUN_MAX_PAYLOAD;

    fr.type   = UTUN_TYPE_DATA;
    fr.flags  = 0;
    fr.length = len;
    memcpy(fr.data, msg, len);

    return utun_write_frame(c->fd, &fr);
}
```

On the room side, this will be unpacked and re-framed into the 0xF0 client
protocol and pushed to the appropriate game tunnel.

---

## How existing services map to uzenet-tunnel

### uzenet-lichess

`uzenet-lichess` already had an almost identical framing:

```c
typedef struct{
    u8 type;
    u8 flags;
    u16 length;
    u8 data[TUNNEL_MAX_PAYLOAD];
} TunnelFrame;
```

and local `ReadTunnelFramed()` / `WriteTunnelFramed()` functions. Migration:

1. Remove its local `TunnelFrame`, `ReadTunnelFramed`, `WriteTunnelFramed`,
   `read_full`, `write_full`.
2. Add `../uzenet-tunnel/uzenet-tunnel.c` to its Makefile sources.
3. `#include "../uzenet-tunnel/uzenet-tunnel.h"`.
4. Replace calls:
   - `ReadTunnelFramed(fd, &fr)` → `utun_read_frame(fd, &fr)`
   - `WriteTunnelFramed(fd, &fr)` → `utun_write_frame(fd, &fr)`
5. Use `UTUN_TYPE_LOGIN` / `UTUN_TYPE_DATA` instead of local `TUNNEL_TYPE_*`.

All the Lichess‑specific payload structs (`LCH_MsgCl*`, `LCH_MsgSv*`) remain
unchanged.

### uzenet-virtual-fujinet

`uzenet-virtual-fujinet` will:

- Expect a LOGIN frame, as above, to learn the `user_id`.
- Use `UTUN_TYPE_DATA` frames where the **payload is the virtual FujiNet
  protocol** (e.g. “FujiNet-PC compatible” commands, or a simplified custom
  subset).
- Optionally use the first byte of the payload as a “sub‑channel” selector
  (control vs disk channel, etc.), if it wants multiple logical sub-tunnels on
  a single AF_UNIX connection.

The Uzebox side only ever sees the **normal client tunnel** protocol
(`0xF0 | tunnel_id` + length + payload). Room is responsible for translating
between Uzebox-tunnel frames and `uzenet-tunnel` frames.

### uzenet-radio

`uzenet-radio` currently uses a line-based ASCII protocol on its AF_UNIX
socket:

- Client sends: `"Stream <url>\n"`, `"Sleep 50\n"`, `"Meta\n"`, etc.
- Server sends: `'A'` + length + PCM bytes, `'Q'` to signal dropped samples.

To integrate with `uzenet-tunnel` without rewriting that logic:

- Room wraps those ASCII commands into `UTUN_TYPE_DATA` frames.
- `uzenet-radio` unwraps them and feeds the payload into its existing
  line parser (for example, treat each DATA frame as an opaque chunk of text).
- Audio output remains as `'A'` frames, but now carried inside
  `UTUN_TYPE_DATA` payloads back to room.

This way, radio’s internal protocol stays the same; only the outer framing
changes.

### uzenet-ssh

`uzenet-ssh` currently:

- Reads a 2‑byte user id at connect time.
- Then passes the raw socket to `uzenet_ssh_handle()`.

With `uzenet-tunnel`:

- Room sends LOGIN (`UTUN_TYPE_LOGIN` + `UtunLoginMeta`).
- All subsequent SSH bytes (terminal I/O) are carried as `UTUN_TYPE_DATA`
  frames.
- `uzenet-ssh` unwraps DATA frames and forwards the payload to the SSH child
  process, and does the reverse for output.

This makes SSH consistent with all other services.

---

## Partial messages and error handling

`uzenet-tunnel` is deliberately **blocking** at the frame level:

- Service code never sees “half of a frame”.
- Either a full header + payload is read, or you get EOF/error.

This simplifies service logic:

- Every call to `utun_read_frame()` yields:
  - `> 0`: a complete, valid frame.
  - `== 0`: clean disconnect (peer closed socket).
  - `< 0`: framing or I/O error (drop client).

If a frame announces a length > `UTUN_MAX_PAYLOAD`, `utun_read_frame()` will:

1. Drain and discard the payload.
2. Return `-1`.

Service code can log this and disconnect the client.

---

## Makefile example (for a service)

Minimal pattern:

```make
CC      := gcc
CFLAGS  := -O2 -Wall -Wextra -pthread
LDFLAGS := -lcurl   # or -lav* etc, per service

SRCS = \
    uzenet-lichess.c \
    ../uzenet-tunnel/uzenet-tunnel.c

OBJS = $(SRCS:.c=.o)

all: uzenet-lichess

uzenet-lichess: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

clean:
	rm -f $(OBJS) uzenet-lichess
```

Other services (`uzenet-virtual-fujinet`, `uzenet-radio`, `uzenet-ssh`) follow
the same pattern: add `../uzenet-tunnel/uzenet-tunnel.c` to `SRCS` and include
the header.

---

## Future extensions

Because all framing is centralized in `uzenet-tunnel`, it’s easy to evolve:

- **Flags**:
  - bit0: “more fragments” for large payloads
  - bit1: “priority” frames
  - etc.
- **New types**:
  - `UTUN_TYPE_CONTROL` for room‑level commands.
  - `UTUN_TYPE_STATS` for periodic service stats back to room.

Existing services can ignore unknown `type` values / `flags` without breaking.

---

## Summary

- `uzenet-tunnel` gives you a **single, robust framing layer** for room↔service
  communication.
- Services only deal with `TunnelFrame` and service‑specific payloads.
- Partial reads/writes and wire encoding are handled centrally.
- Existing services (`lichess`, `radio`, `ssh`) can be migrated incrementally,
  while new services (`uzenet-virtual-fujinet`, etc.) can start on the clean
  API from day one.
