#!/usr/bin/env bash
set -euo pipefail
echo "▶ Removing Uzenet Radio service..."
# Stop and disable the service if it exists
if systemctl list-units --full -all | grep -q uzenet-radio.service; then
  sudo systemctl stop uzenet-radio.service || true
  sudo systemctl disable uzenet-radio.service || true
fi
# Remove the radio binary
sudo rm -f /usr/local/bin/uzenet-radio
