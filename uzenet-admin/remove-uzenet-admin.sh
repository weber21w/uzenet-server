#!/bin/bash
set -e

echo "[*] Removing uzenet-admin-server..."

BIN=/usr/local/bin/uzenet-admin-server
SERVICE=/etc/systemd/system/uzenet-admin.service

sudo systemctl stop uzenet-admin
sudo systemctl disable uzenet-admin
sudo rm -f "$SERVICE"
sudo systemctl daemon-reload

sudo rm -f "$BIN"

echo "[*] uzenet-admin-server removed."
