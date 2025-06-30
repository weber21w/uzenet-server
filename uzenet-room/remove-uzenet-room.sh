#!/bin/bash
set -e
echo "[*] Removing uzenet-room-server..."

BIN=/usr/local/bin/uzenet-room-server
SERVICE=/etc/systemd/system/uzenet-room.service

# Stop and disable service
systemctl stop uzenet-room || true
systemctl disable uzenet-room || true
rm -f "$SERVICE"

# Remove binary
rm -f "$BIN"

# Reload systemd
systemctl daemon-reexec
systemctl daemon-reload

echo "[+] Removed uzenet-room-server."
