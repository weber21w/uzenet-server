#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 3 ]; then
  cat <<EOF
Usage: sudo $0 domain1 [domain2 ...] email

Example:
  sudo $0 example.com www.example.com you@example.org
EOF
  exit 1
fi

# 1) Configure Nginx + SSL
DIR="$(cd "$(dirname "$0")" && pwd)"
echo "→ Running Nginx install script..."
sudo "$DIR/install-nginx.sh" "$@"

# 2) Install the score binary
echo "→ Installing uzenet-score binary..."
install -m 755 "$DIR/uzenet-score" /usr/local/bin/uzenet-score

# 3) Create config directory
echo "→ Deploying default config..."
install -d -m 755 /etc/uzenet-score
install -m 644 "$DIR/config.json" /etc/uzenet-score/config.json

# 4) Register systemd service
echo "→ Writing systemd service file..."
cat > /etc/systemd/system/uzenet-score.service <<EOF
[Unit]
Description=UZENET Score Service
After=network.target

[Service]
Type=simple
ExecStart=/usr/local/bin/uzenet-score --config /etc/uzenet-score/config.json
Restart=on-failure
User=www-data
Group=www-data

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable uzenet-score
systemctl restart uzenet-score

echo "✅ uzenet-score installed and running."
