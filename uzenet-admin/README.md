# Uzenet Admin Server

The `uzenet-admin-server` is a secure web-based control panel for managing the Uzenet identity system and user permissions.

## Features

- Web interface with HTML forms and basic authentication
- Add, remove, and update users in the identity database
- Assign admin (`A`) or founder (`F`) flags
- Allow or revoke IP allowlist access
- Grant developer sidecar upload authority for specific shortnames (e.g. `MEGATR00`)
- Logging of failed logins and unauthorized attempts via `syslog()` (fail2ban compatible)
- Restriction rules:
  - **Admins** can manage regular users
  - **Founders** can manage admins and other founders (only themselves)
  - Prevents privilege escalation

## Port and Access

- Listens on port **9460**
- Web interface is protected using **HTTP Basic Auth**
- Uses `/var/lib/uzenet/users.csv` as the user and permissions source

## Installation

Use the provided Makefile:

```bash
make install
```

This:
- Installs the binary to `/usr/local/bin/`
- Creates a systemd service at `/etc/systemd/system/uzenet-admin.service`
- Starts and enables the service

## Removal

```bash
make remove
```

## Service Status

```bash
make status
```

## Notes

- Does not manage passwords — credentials are stored in `users.csv`
- Authentication uses SHA-256 hashes and long passwords for admin/founder users
- Intended for server operators; Uzebox clients never interact with this service

## Related Services

This server integrates with:

- `uzenet-identity`: Validates credentials and provides identity info
- `fail2ban`: Protects against brute-force or abuse attempts
- `uzenet-*` services: Access control for gameplay, uploads, and sidecars
