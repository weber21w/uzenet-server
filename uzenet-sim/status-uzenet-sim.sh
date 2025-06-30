#!/usr/bin/env bash
set -euo pipefail

echo "=== uzenet-sim-server Service Status ==="
echo

# Binary check
if [ -x /usr/local/bin/uzenet-sim-server ]; then
  echo "Binary:    present (/usr/local/bin/uzenet-sim-server)"
else
  echo "Binary:    missing"
fi

# Unit file check
if [ -f /etc/systemd/system/uzenet-sim-server@.service ]; then
  echo "Unit file: present (/etc/systemd/system/uzenet-sim-server@.service)"
else
  echo "Unit file: missing"
fi

echo
echo "Configured instances:"
systemctl list-unit-files 'uzenet-sim-server@*.service' --no-legend

echo
echo "Active instances:"
systemctl list-units 'uzenet-sim-server@*.service' --no-legend

echo
echo "IPC sockets in /run/uzenet-sim:"
if [ -d /run/uzenet-sim-server ]; then
  ls -1 /run/uzenet-sim-server || echo "  (no sockets present)"
else
  echo "  (directory missing)"
fi
