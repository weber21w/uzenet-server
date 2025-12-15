#!/usr/bin/env bash
set -euo pipefail

echo "▶ Installing Uzenet Identity service..."

# Move to script directory
cd "$(dirname "$0")"

# Build the service
make clean && make

# Copy the binary
sudo cp uzenet-identity /usr/local/bin/uzenet-identity
sudo chmod +x /usr/local/bin/uzenet-identity

# Setup fail2ban filters and jail config
if [[ -x ./setup-fail2ban.sh ]]; then
	echo "⚙️  Running fail2ban setup..."
	sudo ./setup-fail2ban.sh
fi

# Setup user sidecar directories
if [[ -x ./check-uzenet-dev-env.sh ]]; then
	echo "📁 Creating user sidecar dirs..."
	sudo ./check-uzenet-dev-env.sh
fi

# Install systemd service inline (standardized)
SERVICE_FILE="/etc/systemd/system/uzenet-identity.service"
echo "🛠️  Installing systemd unit at $SERVICE_FILE..."
sudo tee "$SERVICE_FILE" > /dev/null <<EOF
[Unit]
Description=Uzenet Identity Service
After=network.target

[Service]
ExecStart=/usr/local/bin/uzenet-identity
Restart=on-failure
User=uzenet
Group=uzenet

[Install]
WantedBy=multi-user.target
EOF

# Enable + restart service
sudo systemctl daemon-reexec
sudo systemctl daemon-reload
sudo systemctl enable uzenet-identity.service
sudo systemctl restart uzenet-identity.service

echo "✅ Uzenet Identity installed."
