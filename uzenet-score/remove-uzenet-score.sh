#!/usr/bin/env bash
set -euo pipefail

echo "→ Stopping uzenet-score service..."
systemctl stop uzenet-score || true
systemctl disable uzenet-score || true

echo "→ Removing systemd unit..."
rm -f /etc/systemd/system/uzenet-score.service
systemctl daemon-reload

echo "→ Deleting binary..."
rm -f /usr/local/bin/uzenet-score

echo "→ Removing config directory..."
rm -rf /etc/uzenet-score

echo "→ Cleaning up Nginx site..."
rm -f /etc/nginx/sites-enabled/uzenet-score.conf \
      /etc/nginx/sites-available/uzenet-score.conf
# reload nginx to drop the site
nginx -s reload || true

echo "✅ uzenet-score removed."
