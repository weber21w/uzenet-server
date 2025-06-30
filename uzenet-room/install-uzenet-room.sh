#!/bin/bash
set -e
echo "[*] Installing uzenet-room-server..."

BIN=uzenet-room-server
TARGET=/usr/local/bin/$BIN
SERVICE=/etc/systemd/system/uzenet-room.service

# Install binary
install -m 755 $BIN "$TARGET"

# Install systemd service
cat <<EOF > "$SERVICE"
[Unit]
Description=Uzenet Room Server
After=network.target

[Service]
ExecStart=$TARGET
Restart=always
User=nobody
Group=nogroup

[Install]
WantedBy=multi-user.target
EOF

# Enable + start
systemctl daemon-reexec
systemctl daemon-reload
systemctl enable uzenet-room
systemctl restart uzenet-room

echo "[+] uzenet-room-server installed and running."
