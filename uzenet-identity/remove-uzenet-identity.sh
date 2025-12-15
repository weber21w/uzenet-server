#!/usr/bin/env bash
set -euo pipefail

echo "🗑️  Removing Uzenet Identity service..."

# Stop and disable systemd service
sudo systemctl stop uzenet-identity.service || true
sudo systemctl disable uzenet-identity.service || true

# Remove service unit
if [[ -f /etc/systemd/system/uzenet-identity.service ]]; then
	echo "🧹 Deleting systemd unit..."
	sudo rm /etc/systemd/system/uzenet-identity.service
fi

# Remove binary
if [[ -f /usr/local/bin/uzenet-identity ]]; then
	echo "🧽 Removing binary..."
	sudo rm /usr/local/bin/uzenet-identity
fi

# Remove fail2ban config if desired (optional)
# sudo rm /etc/fail2ban/filter.d/uzenet-identity.conf
# sudo rm /etc/fail2ban/jail.d/uzenet-identity.local
# sudo systemctl restart fail2ban

# Remove sidecar directories if desired (optional)
# sudo rm -rf /var/lib/uzenet/sidecars

# Reload systemd
sudo systemctl daemon-reexec
sudo systemctl daemon-reload

echo "✅ Uzenet Identity removed."
