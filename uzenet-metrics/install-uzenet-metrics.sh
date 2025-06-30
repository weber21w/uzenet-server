#!/usr/bin/env bash
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
  echo "Must run as root." >&2
  exit 1
fi

echo "→ Installing build dependencies..."
apt update
DEBIAN_FRONTEND=noninteractive apt install -y libmicrohttpd-dev

echo "→ Building uzenet-metrics..."
cd "$(dirname "$0")"
make

echo "→ Installing binary..."
install -m 755 uzenet-metrics /usr/local/bin/uzenet-metrics

echo "→ Creating socket directory..."
mkdir -p /run/uzenet
chown www-data:www-data /run/uzenet
chmod 755 /run/uzenet

echo "→ Writing systemd service unit..."
cat > /etc/systemd/system/uzenet-metrics.service <<EOF
[Unit]
Description=UZENET Metrics Service
After=network.target

[Service]
ExecStart=/usr/local/bin/uzenet-metrics --socket=/run/uzenet/metrics.sock --port=9000
Restart=on-failure
User=www-data
Group=www-data

[Install]
WantedBy=multi-user.target
EOF

echo "→ Enabling and starting service..."
systemctl daemon-reload
systemctl enable uzenet-metrics
systemctl restart uzenet-metrics

echo "✅ uzenet-metrics installed and running."
