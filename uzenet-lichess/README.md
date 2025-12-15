# uzenet-lichess

`uzenet-lichess` is a UzeNet service that proxies Uzebox chess clients to
[Lichess.org](https://lichess.org) using the official board API.

The service:

- Listens on a Unix domain socket (`/run/uzenet/lichess.sock`)
- Accepts framed tunnels from `uzenet-room`
- Uses per-user OAuth tokens from `/var/lib/uzenet/lichess-users`
- Starts open challenges (standard, random opponent), streams the NDJSON
  game feed, and mirrors moves, clocks and chat to the Uzebox.

This document describes:

1. The on-disk token/identity format
2. The tunnel framing
3. The Lichess protocol (CL_*/SV_* messages)
4. Typical message flows for matchmaking, play, history, and chat

---

## 1. User token storage (`lichess-users`)

By default, tokens live here:

```text
/var/lib/uzenet/lichess-users/<user_id>.json
```

Where:

- `<user_id>` is the 16-bit UzeNet identity returned by `uzenet-identity`
- File ownership/permissions are up to you, but usually:

  ```bash
  chown uzenet:uzenet /var/lib/uzenet/lichess-users
  chmod 700 /var/lib/uzenet/lichess-users
  chown uzenet:uzenet /var/lib/uzenet/lichess-users/*.json
  chmod 600 /var/lib/uzenet/lichess-users/*.json
  ```

### 1.1 JSON format

`uzenet-lichess` uses a **very naive** parser; it only looks for `"token"`:

```json
{
  "token": "lip_xxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
}
```

- The `"token"` value must be a Lichess OAuth token with at least:

  - `board:play`
  - `challenge:write`  (for `POST /api/challenge/open`)
  - `board:chat`       (for `POST /api/board/game/:id/chat`)

- Extra fields are ignored.

### 1.2 Shared token fallback

If a per-user file is missing or invalid, `uzenet-lichess` falls back to:

```bash
export LICHESS_SHARED_TOKEN="lip_xxx_shared_xxx"
```

This makes all logins share a single Lichess account (useful for testing).

The resolution order per client:

1. `/var/lib/uzenet/lichess-users/<user_id>.json`
2. `LICHESS_SHARED_TOKEN` environment variable
3. If neither is usable, the server responds with `LCH_ERR_NO_TOKEN`.

---

## 2. Tunnel framing (uzenet-room ↔ uzenet-lichess)

`uzenet-room` talks to `uzenet-lichess` over a Unix socket with a small
frame header:

```c
typedef struct{
    u8  type;   /* TUNNEL_TYPE_* */
    u8  flags;  /* currently unused, 0 */
    u16 length; /* payload length, big-endian, 0..64 */
    u8  data[64];
} TunnelFrame;
```

- `type`:
  - `0x01` = `TUNNEL_TYPE_LOGIN` (meta about who is on this tunnel)
  - `0x02` = `TUNNEL_TYPE_DATA`  (LCH_CL_* / LCH_SV_* payloads)

### 2.1 Login meta frame

On new tunnel connect, `uzenet-room` should send exactly one `LOGIN` frame:

```c
typedef struct{
    u16 user_id;
    u16 reserved;
} TunnelLoginMeta;
```

- `user_id`: UzeNet identity (2-byte value from `uzenet-identity`)
- `reserved`: must be 0 for now

**Server behavior:**

1. `uzenet-lichess` receives `TUNNEL_TYPE_LOGIN` + `TunnelLoginMeta`
2. It loads `/var/lib/uzenet/lichess-users/<user_id>.json` or shared token
3. It **immediately enqueues** an `LCH_SV_HELLO` back to the client via
   `TUNNEL_TYPE_DATA` (see below)

After `LOGIN`, all subsequent frames on the tunnel must use
`TUNNEL_TYPE_DATA` and carry a single LCH message:

- Uzebox → server: LCH_CL_*
- Server → Uzebox: LCH_SV_*

---

## 3. Lichess protocol (Uzebox ↔ uzenet-lichess)

All game/chat logic is carried inside `TUNNEL_TYPE_DATA` frames as small
fixed-layout structs defined in `uzenet-lichess.h`.

### 3.1 Client → Server messages (LCH_CL_*)

Short summary (full layouts are in the header):

```c
/* Hello / version handshake */
LCH_CL_HELLO        = 0x01;

/* Request a new random opponent game */
LCH_CL_NEW_GAME     = 0x02;

/* Make a move (0..63 squares, plus promotion) */
LCH_CL_MOVE         = 0x03;

/* Resign current game */
LCH_CL_RESIGN       = 0x04;

/* Abort game (only valid before both sides have moved) */
LCH_CL_ABORT        = 0x05;

/* Ping for heartbeat / latency measurement */
LCH_CL_PING         = 0x06;

/* Send a chat line */
LCH_CL_CHAT         = 0x07;

/* Request a window of move history (for scrolling) */
LCH_CL_REQ_MOVES    = 0x08;

/* Request a window of chat history (for scrolling) */
LCH_CL_REQ_CHAT     = 0x09;
```

The important ones:

#### LCH_CL_NEW_GAME

```c
typedef struct{
    u8 type;      /* LCH_CL_NEW_GAME */
    u8 flags;     /* rated + color preference */
    u8 minutes;   /* base time in minutes */
    u8 increment; /* seconds increment per move */
} LCH_MsgClNewGame;
```

- `flags`:
  - `LCH_FLAG_RATED` (0x01) => rated vs casual
  - `LCH_COLOR_AUTO` (0x00) / `LCH_COLOR_WHITE` / `LCH_COLOR_BLACK`
    *are reserved bits* – server currently always uses random color.

Server behavior:

- Calls `lichess_start_random_game()` → `POST /api/challenge/open`
- On success, stores game ID and starts the streaming thread
- Sends `LCH_SV_GAME_START` back to client

#### LCH_CL_MOVE

```c
typedef struct{
    u8 type;    /* LCH_CL_MOVE */
    u8 from_sq; /* 0..63 */
    u8 to_sq;   /* 0..63 */
    u8 promo;   /* LCH_PROMO_* */
} LCH_MsgClMove;
```

- Server converts to UCI via `lch_move_to_uci()` and calls:

  `POST /api/board/game/:id/move/:uci`

- It also appends the move to the server-side move log for scrolling.

#### LCH_CL_CHAT

```c
typedef struct{
    u8   type;   /* LCH_CL_CHAT */
    u8   flags;  /* reserved */
    u8   len;    /* 0..LCH_CHAT_MAX_LEN */
    char text[LCH_CHAT_MAX_LEN];
} LCH_MsgClChat;
```

Server behavior:

1. Adds `"You: <text>"` to its local chat log (for history)
2. Immediately sends `LCH_SV_CHAT` back so the client sees its own line
3. Sends `POST /api/board/game/:id/chat` to Lichess (room=player)

#### LCH_CL_REQ_MOVES / LCH_CL_REQ_CHAT

```c
typedef struct{
    u8 type;     /* LCH_CL_REQ_MOVES or LCH_CL_REQ_CHAT */
    u8 start_hi; /* high byte of start index */
    u8 start_lo; /* low byte of start index */
    u8 count;    /* number to return (0 = default limit) */
} LCH_MsgClReqMoves, LCH_MsgClReqChat;
```

- `start = (start_hi << 8) | start_lo`
- `count = 0` means "use a reasonable default" (server clamps to
  <= 32 plies for moves, <= 20 lines for chat)

Server responds with multiple `LCH_SV_OPP_MOVE` or `LCH_SV_CHAT`
messages containing the requested window, plus an optional `LCH_SV_INFO`
with the total count.

---

### 3.2 Server → Client messages (LCH_SV_*)

Short list:

```c
LCH_SV_HELLO      = 0x81; /* protocol/version hello */
LCH_SV_GAME_START = 0x82; /* new game created/matched */
LCH_SV_OPP_MOVE   = 0x83; /* move from opponent OR history */
LCH_SV_GAME_END   = 0x84; /* final result/reason */
LCH_SV_ERROR      = 0x85; /* error code */
LCH_SV_PONG       = 0x86; /* ping reply */
LCH_SV_INFO       = 0x87; /* info/metadata (move count, chat count, etc.) */
LCH_SV_CLOCK      = 0x88; /* clock snapshot */
LCH_SV_CHAT       = 0x89; /* chat line (live or replay) */
```

Key ones:

#### LCH_SV_HELLO

Sent twice (potentially):

1. Automatically by server immediately after LOGIN meta
2. Optionally again in response to `LCH_CL_HELLO` (client might send one)

```c
typedef struct{
    u8 type;        /* LCH_SV_HELLO */
    u8 proto_ver;   /* currently 1 */
    u8 capabilities;/* reserved */
    u8 reserved;
} LCH_MsgSvHello;
```

Client can treat any HELLO as "ok, link is up, you can show title screen".

#### LCH_SV_GAME_START

```c
typedef struct{
    u8  type;        /* LCH_SV_GAME_START */
    u8  flags;       /* mirrors CL_NEW_GAME flags */
    u8  minutes;
    u8  increment;
    u8  my_side;     /* LCH_SIDE_WHITE/BLACK or 0xFF unknown */
    u8  game_id_len;
    char game_id[8]; /* short lichess game/challenge ID */
} LCH_MsgSvGameStart;
```

- `my_side` is currently 0xFF until we parse `gameFull` (TODO).
- `game_id` is optional and just for debugging / display.

#### LCH_SV_OPP_MOVE

```c
typedef struct{
    u8 type;    /* LCH_SV_OPP_MOVE */
    u8 from_sq;
    u8 to_sq;
    u8 promo;
} LCH_MsgSvOppMove;
```

Used for:

- Live opponent moves (from Lichess stream)
- **History replays** when serving `LCH_CL_REQ_MOVES`

Client must distinguish by UI context; the message format is the same.

#### LCH_SV_CLOCK

```c
typedef struct{
    u8 type;       /* LCH_SV_CLOCK */
    u8 flags;      /* bit0: side to move (0=white, 1=black) */
    u16 white_secs;
    u16 black_secs;
} LCH_MsgSvClock;
```

Derived from `wtime`/`btime` in `gameState` NDJSON.

#### LCH_SV_CHAT

```c
typedef struct{
    u8   type;   /* LCH_SV_CHAT */
    u8   flags;  /* bit0=1 for local player echo, 0 for remote/history */
    u8   len;    /* 0..LCH_CHAT_MAX_LEN */
    char text[LCH_CHAT_MAX_LEN];
} LCH_MsgSvChat;
```

Sent when:

- Local user sends chat: echo line with `flags |= 1`
- Remote chat arrives via Lichess `chatLine` event (`flags = 0`)
- History replay in response to `LCH_CL_REQ_CHAT` (`flags = 0`)

The server also keeps a chat log (up to `LCH_MAX_CHAT_LINES` per game).

#### LCH_SV_GAME_END

```c
typedef struct{
    u8 type;   /* LCH_SV_GAME_END */
    u8 result; /* LCH_RESULT_* from client POV (may be unknown) */
    u8 reason; /* LCH_REASON_* (checkmate, resign, timeout, etc.) */
    u8 reserved;
} LCH_MsgSvGameEnd;
```

- Triggered when `gameState.status` != `"started"` in NDJSON.
- Result is partially inferred from status; winner/loser is not yet
  derived from the `winner` field (TODO).

#### LCH_SV_ERROR

```c
typedef struct{
    u8 type;  /* LCH_SV_ERROR */
    u8 code;  /* LCH_ERR_* */
    u8 arg0;
    u8 arg1;
} LCH_MsgSvError;
```

Main error codes used:

- `LCH_ERR_NO_TOKEN`      – no per-user or shared token
- `LCH_ERR_LICHESS_HTTP`  – HTTP failure (arg0 = -return_code)
- `LCH_ERR_ALREADY_GAME`  – client tried NEW_GAME while already in game
- `LCH_ERR_NO_GAME`       – action requires a current game
- `LCH_ERR_GENERIC`       – unknown message type, etc.

---

## 4. Typical flows

### 4.1 Tunnel + hello

1. `uzenet-room` accepts a client, decides it wants Lichess service.
2. `uzenet-room` opens `/run/uzenet/lichess.sock`.
3. Sends `TUNNEL_TYPE_LOGIN` frame with `TunnelLoginMeta{user_id}`.
4. `uzenet-lichess` loads token and enqueues `LCH_SV_HELLO`.

Uzebox sees `LCH_SV_HELLO` → shows title screen.

Client may optionally send `LCH_CL_HELLO` and get another `LCH_SV_HELLO`.

### 4.2 New game + streaming

1. Uzebox picks time control on title screen, sends:

   ```c
   LCH_MsgClNewGame ng = { LCH_CL_NEW_GAME, flags, minutes, increment };
   ```

2. Server calls `lichess_start_random_game()`:

   - `POST /api/challenge/open`
   - Stores `game_id`
   - Clears move/chat logs

3. Server sends `LCH_SV_GAME_START` with:

   - `flags`, `minutes`, `increment`
   - `my_side = 0xFF` (for now)
   - short `game_id` (optional)

4. Server launches `stream_thread_main()`:

   - `GET /api/board/game/stream/:game_id`
   - For each `gameState` line:
     - Updates clocks → `LCH_SV_CLOCK`
     - Adds last move to move log + sends `LCH_SV_OPP_MOVE`
     - If status != `"started"` → `LCH_SV_GAME_END`
   - For each `chatLine` line:
     - Adds a formatted `user: text` to chat log + sends `LCH_SV_CHAT`

### 4.3 Moves

- Uzebox sends `LCH_CL_MOVE`.
- Server converts to UCI + calls Lichess.
- Server appends to move log immediately.
- When NDJSON echoes the new last move:

  - If it matches `last_sent_uci`, server **does not** send another
    `LCH_SV_OPP_MOVE` (filters own moves).
  - If it differs, it must be a remote move → send `LCH_SV_OPP_MOVE`.

### 4.4 Chat

- Local user types chat → `LCH_CL_CHAT`.
- Server:

  1. Adds `"You: text"` to chat log.
  2. Immediately sends `LCH_SV_CHAT` with `flags |= 1`.
  3. `POST /api/board/game/:id/chat`.

- Remote user chat events arrive as `chatLine` in NDJSON:

  - Server builds `"user: text"` string.
  - Appends to chat log.
  - Sends `LCH_SV_CHAT` with `flags = 0`.

### 4.5 History scrolling (no SPI RAM required)

On Uzebox, when the user scrolls moves or chat:

- Send `LCH_CL_REQ_MOVES` with start index and count.
- Or `LCH_CL_REQ_CHAT` with start index and count.

Server uses its in-RAM logs:

- Sends up to N `LCH_SV_OPP_MOVE` or `LCH_SV_CHAT` in order.
- Then an optional `LCH_SV_INFO`:

  - `info_code = LCH_INFO_MOVES_TOTAL` or `LCH_INFO_CHAT_TOTAL`
  - `value0/value1` contain the total count (big-endian)

Client can use total count to clamp scrollbars and detect “top/bottom”.

### 4.6 End of game

- When `LCH_SV_GAME_END` is received:

  - Game is over, but the server still keeps move/chat logs.
  - Uzebox can still ask for `REQ_MOVES` / `REQ_CHAT` to review.
  - When the tunnel eventually closes, all client state is freed.

---

## 5. Notes / TODO

- `my_side` is currently always `0xFF`. To set it correctly, stream
  parsing should look at the `gameFull` event and its `white.id` /
  `black.id` vs the token’s user, then set `LCH_SIDE_WHITE/BLACK`.

- Some advanced Lichess events (opponentGone, draw offers, etc.) are not
  wired yet. Basic play, clocks, chat, and history are functional.

- `LCH_MAX_MOVES` and `LCH_MAX_CHAT_LINES` are per-client buffers and
  can be tuned; current defaults are:

  ```c
  #define LCH_MAX_MOVES      512  /* plies */
  #define LCH_MAX_CHAT_LINES 256
  ```

---
