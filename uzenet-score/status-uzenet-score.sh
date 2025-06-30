#!/usr/bin/env bash

echo "=== uzenet-score Service Status ==="

if systemctl is-active --quiet uzenet-score; then
  echo "Service:    active (running)"
else
  echo "Service:    inactive"
fi

if systemctl is-enabled --quiet uzenet-score; then
  echo "Enabled:    yes"
else
  echo "Enabled:    no"
fi

echo
echo "Binary (/usr/local/bin/uzenet-score):" \
     $( [ -x /usr/local/bin/uzenet-score ] && echo "present" || echo "missing" )
echo "Config (/etc/uzenet-score/config.json):" \
     $( [ -f /etc/uzenet-score/config.json ] && echo "present" || echo "missing" )
echo "Nginx site (/etc/nginx/sites-enabled/uzenet-score.conf):" \
     $( [ -e /etc/nginx/sites-enabled/uzenet-score.conf ] && echo "enabled" || echo "disabled" )

echo
echo "Listening ports for uzenet-score:"
ss -tnlp | grep uzenet-score || echo "  (no sockets open)"
