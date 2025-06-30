#!/usr/bin/env bash
set -euo pipefail
echo "▶ Installing Uzenet Radio service..."
# Copy or build the radio binary into place (example path)
sudo cp uzenet-radio /usr/local/bin/uzenet-radio
sudo chmod +x /usr/local/bin/uzenet-radio
# Enable and start service if a systemd unit exists
if systemctl list-unit-files | grep -q uzenet-radio.service; then
  sudo systemctl enable uzenet-radio.service
  sudo systemctl restart uzenet-radio.service
fi
