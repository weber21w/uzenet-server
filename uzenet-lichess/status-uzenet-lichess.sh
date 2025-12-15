#!/usr/bin/env bash
set -euo pipefail

echo "=== uzenet-lichess Service Status ==="

if systemctl is-active --quiet uzenet-lichess; then
	echo "Service:    active (running)"
else
	echo "Service:    inactive"
fi

if systemctl is-enabled --quiet uzenet-lichess; then
	echo "Enabled:    yes"
else
	echo "Enabled:    no"
fi

echo
echo "Binary (/usr/local/bin/uzenet-lichess):" \
     $( [ -x /usr/local/bin/uzenet-lichess ] && echo "present" || echo "missing" )
echo "Socket (/run/uzenet/lichess.sock):" \
     $( [ -e /run/uzenet/lichess.sock ] && echo "present" || echo "not found" )
echo "Env (/etc/uzenet/lichess.env):" \
     $( [ -f /etc/uzenet/lichess.env ] && echo "present" || echo "missing" )
echo "User tokens (/var/lib/uzenet/lichess-users):" \
     $( [ -d /var/lib/uzenet/lichess-users ] && echo "present" || echo "missing" )

echo
echo "Active Unix sockets for uzenet-lichess:"
ss -xlnp 2>/dev/null | grep 'uzenet-lichess' || echo "  (no matching sockets)"
