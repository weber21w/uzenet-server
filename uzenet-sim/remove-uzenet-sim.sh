#!/usr/bin/env bash
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
  echo "Error: must run as root." >&2
  exit 1
fi

echo "→ Stopping & disabling all uzenet-sim-server instances…"
systemctl stop 'uzenet-sim-server@*' 2>/dev/null || true
systemctl disable 'uzenet-sim-server@*' 2>/dev/null || true

echo "→ Removing systemd template…"
rm -f /etc/systemd/system/uzenet-sim-server@.service
systemctl daemon-reload

echo "→ Removing binary…"
rm -f /usr/local/bin/uzenet-sim-server

echo "→ Cleaning IPC directory…"
rm -rf /run/uzenet-sim-server

echo "✅ uzenet-sim-server removed."
