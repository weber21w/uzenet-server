#!/usr/bin/env bash
set -euo pipefail

# Usage: sudo ./uzenet-irc-setup.sh [NETWORK_NAME] [ADMIN_EMAIL]
NETWORK_NAME="${1:-UZENET}"
HOSTNAME_FQDN="$(hostname -f)"
ADMIN_EMAIL="${2:-admin@${HOSTNAME_FQDN}}"

echo "→ Updating package lists..."
apt update

echo "→ Installing ngIRCd..."
DEBIAN_FRONTEND=noninteractive apt install -y ngircd

# Backup existing config if present
if [ -f /etc/ngircd/ngircd.conf ]; then
  echo "→ Backing up existing configuration..."
  mv /etc/ngircd/ngircd.conf /etc/ngircd/ngircd.conf.bak.$(date +%Y%m%d_%H%M%S)
fi

echo "→ Writing new ngIRCd configuration..."
cat > /etc/ngircd/ngircd.conf <<EOF
[Global]
Name = ${HOSTNAME_FQDN}
Info = "UZENET IRC Server"
Network = "${NETWORK_NAME}"
AdminName = "Uzenet Admin"
AdminNick = "admin"
AdminEmail = "${ADMIN_EMAIL}"
BindAddress = 0.0.0.0

[ServerDefault]
Listen = 6667
Allow = *@*

[Log]
; log everything to syslog
Level = NOTICE
EOF

echo "→ Enabling and starting ngIRCd service..."
systemctl enable ngircd
systemctl restart ngircd

# UFW firewall rule
if command -v ufw >/dev/null 2>&1; then
  echo "→ Allowing IRC port 6667 through UFW..."
  ufw allow 6667/tcp
fi

echo
echo "✅ ngIRCd is up and running!"
echo "   • Server name: ${HOSTNAME_FQDN}"
echo "   • Network:     ${NETWORK_NAME}"
echo "   • Port:        6667"
echo "   • Admin email: ${ADMIN_EMAIL}"
echo
echo "Clients can now connect with:"
echo "    /connect ${HOSTNAME_FQDN}:6667"
