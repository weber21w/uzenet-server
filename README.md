# Uzenet Service Suite Overview

## 📡 Uzenet Service Suite

The **Uzenet** system is a suite of minimal yet powerful C-based services that enable multiplayer games, networked utilities, and streaming media for the Uzebox retro game console and other lightweight embedded clients.

---

## 🧩 Architecture Overview

```
+-------------------------+
|      Uzebox Game       |
|------------------------|
| UART ↔ Uzenet Protocol |
+-------------------------+
           │
           ▼
+-------------------------+       +-----------------------+
|   uzenet-core (firmware)│ <---> |   uzenet-identity     |
+-------------------------+       +-----------------------+
           │                           ▲
           ▼                           │
+-------------------------+            │
|     Network Services    |◄──────────┘
+-------------------------+
│ uzenet-room             │ ◄──> Lobby, Matchmaking, Chat
│ uzenet-radio            │ ◄──> Audio Streamer (FM/MP3/etc)
│ uzenet-zipstream        │ ◄──> On-the-fly ZIP streaming
│ uzenet-lynx             │ ◄──> Web Browser Proxy
│ uzenet-fatfs            │ ◄──> Virtual File System
│ uzenet-sim              │ ◄──> Emulator-Driven Sim Loops
│ uzenet-score            │ ◄──> High Score Service
│ uzenet-irc              │ ◄──> IRC Gateway
│ uzenet-metrics          │ ◄──> Server Analytics
+-------------------------+
           │
           ▼
+--------------------------+
| uzenet-admin-server      |
| (Web-based Admin Panel)  |
+--------------------------+
```

---

## 🔐 Identity and Security

- **uzenet-identity**: Provides centralized authentication for all services. Uses a UNIX socket, SHA-256 password hashes, and optional developer roles.
- **Developer Upload Auth**: Long-password HTTP auth on port 9461 allows uploading of game-specific sidecar services.

---

## ⚙️ Installation and Startup

Each service includes:
- A `Makefile` with install/remove/status targets
- `install-*.sh` and `remove-*.sh` scripts
- Logs to `syslog` in a `fail2ban`-friendly format

You should install at least:
```bash
cd uzenet-identity && make install
```

Optionally:
```bash
cd uzenet-admin && make install
```

All other services can be installed or removed similarly.

A universal `administer-users.sh` dialog script is included for user/dev management.

---

## 🌐 Core Services and Roles

| Service            | Description |
|--------------------|-------------|
| **uzenet-identity** | Verifies all users across services. Supports guest, registered, and developer roles. |
| **uzenet-room**     | Chatrooms, matchmaking, command relays, TCP bridge negotiation. |
| **uzenet-radio**    | Streams audio using FFmpeg (15.72kHz mono) to Uzebox. |
| **uzenet-lynx**     | Converts web pages to text using `lynx` and streams updates. |
| **uzenet-irc**      | Maps Uzebox terminals to IRC channels and users. |
| **uzenet-fatfs**    | Acts as a remote SD card; FAT-compatible file API over UART. |
| **uzenet-zipstream**| Unzips and streams compressed files over HTTP/TCP with no temp files. |
| **uzenet-sim**      | Handles deterministic logic loops for turn-based or lockstep games. |
| **uzenet-score**    | Records and lists game high scores. |
| **uzenet-metrics**  | Tracks and exposes internal metrics for debugging. |
| **uzenet-admin**    | Web interface for identity and permission management. |

---

## 🧑‍💼 Admin and Developer Flow

- Founders: Cannot be modified or deleted by anyone else.
- Admins: Can manage users, but not other admins or founders.
- Developers: Can upload custom game-specific sidecars.

**Upload Flow**:
1. Admin marks user as authorized for game `XYZGME01`
2. Developer uses `curl` + HTTP Basic Auth to upload binary sidecar to port 9461.

---

## 📑 Notes

- Each service includes its own `README.md` with details.
- Service interactions rely on text-based protocols.
- Clients may share UART connections using a simple session state machine in `uzenetCore`.
