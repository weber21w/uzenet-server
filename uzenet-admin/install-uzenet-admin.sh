#!/bin/bash
set -e

echo "[*] Installing uzenet-admin-server..."

BIN=/usr/local/bin/uzenet-admin-server
SERVICE=/etc/systemd/system/uzenet-admin.service

# Copy binary
sudo cp uzenet-admin-server "$BIN"
sudo chmod 755 "$BIN"

# Create systemd service
sudo tee "$SERVICE" > /dev/null <<EOF
[Unit]
Description=Uzenet Admin Panel
After=network.target

[Service]
ExecStart=$BIN
Restart=on-failure

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reexec
sudo systemctl daemon-reload
sudo systemctl enable uzenet-admin
sudo systemctl restart uzenet-admin

# Ensure embedded cmark files exist
[ ! -f cmark_embedded.c ] && echo "[!] Missing cmark_embedded.c!" && exit 1
[ ! -f cmark_embedded.h ] && echo "[!] Missing cmark_embedded.h!" && exit 1

echo "[*] uzenet-admin-server installed and started."
