# Uzenet IRC Server

The `uzenet-irc-server` acts as a smart bridge between Uzebox clients and a traditional IRC network. It allows embedded Uzebox systems to participate in global chat rooms with minimal resource overhead.

## Features

- Lightweight IRC proxy tailored for 8-bit clients
- Translates simplified Uzebox chat messages into IRC-compatible commands
- Optionally filters or throttles outbound messages to protect IRC servers
- Bridges to public or private IRC channels
- Can map each Uzebox to a unique IRC nickname
- Supports room-to-IRC mapping (e.g. room `MEGATR00` → channel `#MEGATR00`)
- May support Web access to IRC rooms via future integration with web frontend

## Port

- Listens on a configurable TCP port (default TBD)
- Connects upstream to a full IRC server (e.g. `irc.libera.chat`)

## Installation

Use the provided Makefile:

```bash
make install
```

This:
- Installs the IRC bridge binary
- Enables and starts the `uzenet-irc` systemd service

## Removal

```bash
make remove
```

## Service Status

```bash
make status
```

## Logging and Security

- Logs failed upstream connections or malformed client messages via `syslog()`
- Integrates with `fail2ban` for abusive behavior protection
- Future versions may support username-based filtering or chat moderation

## Use Cases

- Enable real-time cross-platform chat in Uzebox online games
- Export game room chat to a visible IRC channel
- Permit browser-based IRC monitoring of Uzebox sessions

## Related Services

- `uzenet-room`: May route per-room chat into IRC via this bridge
- `uzenet-admin-server`: Controls access and filters
- `uzenet-voice`: Complements IRC with audio features
