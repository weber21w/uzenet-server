#!/usr/bin/env bash
set -euo pipefail

echo "=== uzenet-metrics Service Status ==="

if systemctl is-active --quiet uzenet-metrics; then
  echo "Service:    active (running)"
else
  echo "Service:    inactive"
fi

if systemctl is-enabled --quiet uzenet-metrics; then
  echo "Enabled:    yes"
else
  echo "Enabled:    no"
fi

echo
echo "Binary (/usr/local/bin/uzenet-metrics):" \
     $( [ -x /usr/local/bin/uzenet-metrics ] && echo "present" || echo "missing" )
echo "Socket (/run/uzenet/metrics.sock):" \
     $( [ -e /run/uzenet/metrics.sock ] && echo "listening" || echo "not found" )

echo
echo "Listening TCP port (9000):"
ss -tnlp 2>/dev/null | grep '9000' || echo "  (not listening)"
