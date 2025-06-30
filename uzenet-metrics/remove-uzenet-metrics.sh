#!/usr/bin/env bash
set -euo pipefail

echo "→ Stopping uzenet-metrics..."
systemctl stop uzenet-metrics || true
systemctl disable uzenet-metrics || true

echo "→ Removing systemd unit..."
rm -f /etc/systemd/system/uzenet-metrics.service
systemctl daemon-reload

echo "→ Deleting binary..."
rm -f /usr/local/bin/uzenet-metrics

echo "→ Cleaning up socket directory..."
rm -rf /run/uzenet/metrics.sock

echo "✅ uzenet-metrics removed."
