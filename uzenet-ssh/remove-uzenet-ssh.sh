#!/usr/bin/env bash
set -euo pipefail

echo "🗑 Removing Uzenet SSH service..."
sudo systemctl stop uzenet-ssh.service || true
sudo systemctl disable uzenet-ssh.service || true
sudo rm -f /etc/systemd/system/uzenet-ssh.service
sudo rm -f /usr/local/bin/uzenet-ssh
sudo systemctl daemon-reload
echo "✅ Uzenet SSH removed."
