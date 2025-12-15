#!/usr/bin/env bash
set -euo pipefail

echo "▶ Building Uzenet SSH service..."
make

echo "▶ Installing uzenet-ssh binary..."
sudo cp uzenet-ssh /usr/local/bin/uzenet-ssh
sudo chmod +x /usr/local/bin/uzenet-ssh

echo "▶ Installing systemd service..."
sudo cp uzenet-ssh.service /etc/systemd/system/uzenet-ssh.service
sudo systemctl daemon-reexec
sudo systemctl daemon-reload
sudo systemctl enable uzenet-ssh.service
sudo systemctl restart uzenet-ssh.service

echo "✅ Uzenet SSH installed and running."
