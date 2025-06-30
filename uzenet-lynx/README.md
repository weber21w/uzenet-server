# Uzenet Lynx Server

The `uzenet-lynx-server` provides a text-mode web browsing interface for Uzebox using a modified Lynx backend. It streams simplified webpages to the Uzebox and allows navigation via low-bandwidth controls.

## Features

- Converts web pages to plain-text for display on Uzebox
- Supports links, basic forms, and scrolling
- Sends screen updates using RLE or delta-encoded format
- Allows URL entry and forward/back navigation from the Uzebox
- May provide bookmarks, recent sites, and filtered search integration
- Fully keyboard-navigable with minimal memory usage on the client
- Optionally logs visited sites and link metrics for usage tracking

## Port

- Listens on TCP port **57429**

## Installation

Use the provided Makefile:

```bash
make install
```

This:
- Installs the binary to `/usr/local/bin/`
- Sets up and starts the `uzenet-lynx` systemd service

## Removal

```bash
make remove
```

## Service Status

```bash
make status
```

## Logging and Security

- Logs requests, page load failures, and malformed URLs via `syslog()`
- Can restrict URLs or domains with optional allowlist or denylist
- Failures logged in a `fail2ban`-compatible format

## Use Cases

- Browse documentation, BBS-style forums, or game hints from within Uzebox
- Provide a landing/startup ROM for users to explore Uzenet services
- Link to scores, chat logs, or multiplayer game lobbies
- Pair with Uzenet identity for restricted access to internal docs

## Related Services

- `uzenet-identity`: Optional authentication for page filtering
- `uzenet-score`: May link to live game stats or screenshots
- `uzenet-room`: Game Room ROM may use Lynx mode as a launcher shell
