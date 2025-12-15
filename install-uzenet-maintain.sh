#!/usr/bin/env bash
set -euo pipefail

echo "🛠 Installing uzenet-maintain systemd timer..."

TIMER_PATH="/etc/systemd/system/uzenet-maintain.timer"
SERVICE_PATH="/etc/systemd/system/uzenet-maintain.service"
SCRIPT_PATH="/srv/uzenet-master/uzenet-maintain.sh"

# Create service unit
sudo tee "$SERVICE_PATH" >/dev/null <<EOF
[Unit]
Description=Run periodic maintenance for Uzenet

[Service]
Type=oneshot
ExecStart=$SCRIPT_PATH
EOF

# Create timer unit
sudo tee "$TIMER_PATH" >/dev/null <<EOF
[Unit]
Description=Run uzenet-maintain every 12 hours

[Timer]
OnBootSec=5min
OnUnitActiveSec=12h
Persistent=true

[Install]
WantedBy=timers.target
EOF

# Reload and enable
sudo systemctl daemon-reexec
sudo systemctl daemon-reload
sudo systemctl enable --now uzenet-maintain.timer

echo "✅ Timer installed and started: uzenet-maintain.timer"
