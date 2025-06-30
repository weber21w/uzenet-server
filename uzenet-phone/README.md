# Uzenet Phone Server

The `uzenet-phone-server` enables two-way audio communication between Uzebox consoles or between Uzebox and external systems. It acts as a virtual telephone switchboard over TCP.

## Features

- Real-time compressed voice transmission using custom ADPCM codec
- Call routing via short numeric or user-based identifiers
- Optional integration with web clients or physical phone gateways (future)
- Basic "ring", "accept", and "hangup" signaling protocol
- Client-side handles push-to-talk or continuous voice (configurable)
- Logs call attempts, durations, and errors for admin tracking

## Port

- Listens on TCP port **57430**

## Installation

```bash
make install
```

This:
- Installs the binary
- Enables and starts the `uzenet-phone` systemd service

## Removal

```bash
make remove
```

## Service Status

```bash
make status
```

## Logging and Security

- Logs voice connection attempts and failures using `syslog()`
- Integrates with `fail2ban` for login and abuse protection
- Future: allow only specific pairs or groupings (whitelist)

## Use Cases

- Voice chat during multiplayer games
- Direct player-to-player calling from Game Room ROM
- Lobby intercom or broadcast mode (server → all)
- Support for developer testing with web or desktop clients

## Related Services

- `uzenet-identity`: Authenticates users and manages phone permissions
- `uzenet-room`: Voice can optionally be enabled per game room
- `uzenet-bridge`: May relay voice between NAT-blocked clients
