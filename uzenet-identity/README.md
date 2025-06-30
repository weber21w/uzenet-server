# Uzenet Identity Server

The `uzenet-identity-server` is a secure local authentication service for all Uzenet components. It provides username, password, flag, and permission lookup for Uzenet programs and supports developer access control for uploads.

## Features

- Authenticates usernames and SHA-256 password hashes from `users.csv`
- Responds to queries over a UNIX socket (`/run/uzenet/identity.sock`)
- Flags returned include:
  - `R` = Registered user
  - `G` = Guest
  - `A` = Admin
  - `F` = Founder (cannot be modified by others)
- Supports three name formats per user:
  - Full name (13 characters)
  - Short name (8 characters)
  - ID name (6 characters)
- Developer-sidecar access for uploading authorized game logic (e.g. `NEWGME01`)
- Separate secure HTTPS port for developer uploads (planned, e.g. port 9461)
- Permanent and temporary IP allowlist support (via Fail2Ban integration)
- Logs unauthorized access and bad password attempts in `fail2ban`-compatible format via `syslog()`

## Installation

Use the provided Makefile:

```bash
make install
```

This:
- Installs the identity service to `/usr/local/bin/`
- Creates a systemd service at `/etc/systemd/system/uzenet-identity.service`
- Starts and enables the service

## Configuration

- Database: `/var/lib/uzenet/users.csv`
- Socket: `/run/uzenet/identity.sock`
- Cover Directory: `/var/lib/uzenet/covers/USERNAME/` (created automatically for devs)

## Developer Authorization

Administrators can grant upload rights to developers for specific game shortnames.
The following are supported:
- Add/remove user
- Set flags (R/G/A/F)
- Grant dev access to a game ID
- Allowlist IPs (permanent or temporary)

This is managed via:
- `uzenet-admin-server` (web GUI)
- `administer-users.sh` (dialog-based TUI)
- Periodic permission enforcement (via cron-safe helper script)

## Removal

```bash
make remove
```

## Service Status

```bash
make status
```

## Notes

- All logs routed through `syslog()` for system-level auditing
- Designed for internal use only; never exposed to the public internet
- Used by: `uzenet-room`, `uzenet-score`, `uzenet-sim`, and other core services

## Related Services

- `uzenet-admin-server`: Web UI for managing users
- `uzenet-score`: May validate uploads against identity roles
- `uzenet-room`: Authenticates users joining multiplayer sessions
