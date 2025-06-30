# Uzenet FatFS Server

The `uzenet-fatfs-server` is a networked FAT-style file service for Uzebox programs. It emulates a read-only SD card, delivering file contents and directory metadata via TCP.

## Features

- Provides a networked replacement for Petit FatFS (PFF)
- Clients can:
  - Mount the virtual filesystem
  - Read directory entries
  - Retrieve file contents in full or by sector
- Designed for low-memory clients (Uzebox) using UART or TCP
- Sector-aligned `.bd` (binary database) file format optimized for fast lookups
- Supports file filtering (e.g. extensions or flags) and partial reads
- Future support for long filenames, file CRCs, and deduplicated names
- Separate `uzenetfs` client library available for Uzebox

## Server Port

- Listens on **TCP port 57428**

## Installation

Use the provided Makefile:

```bash
make install
```

This:
- Installs the binary to `/usr/local/bin/`
- Creates a systemd service at `/etc/systemd/system/uzenet-fatfs.service`
- Starts and enables the service

## Database Format

- `.bd` file contains:
  - 512-byte header
  - Padded file name and length tables
  - Optional long names
  - File contents padded to 512-byte sectors
- Can be created or modified using the `udbtool` CLI utility

## Client API

- Expects request codes over TCP from a single Uzebox client per connection
- Can stream file sectors efficiently in response to range requests
- Future plans include:
  - File metadata retrieval (e.g. CRCs)
  - Runtime filtering of file types
  - Optional compression

## Removal

```bash
make remove
```

## Service Status

```bash
make status
```

## Related Tools

- `udbtool`: CLI tool for building `.bd` database files
- `ufs.c`: Uzebox client interface using UART or TCP
- `uzenet-identity`: For authentication if write support is enabled in the future

## Notes

- Read-only for now, designed for maximum reliability and predictability
- Writes, uploads, and deletions are not currently supported
- All logs routed via `syslog()` and compatible with `fail2ban`
