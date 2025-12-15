#!/bin/bash

# Configuration
DOMAIN="room.uzenet.us"
EMAIL="admin@example.com"
CERT_PATH="/etc/letsencrypt/live/$DOMAIN/fullchain.pem"
KEY_PATH="/etc/letsencrypt/live/$DOMAIN/privkey.pem"
SERVER_RELOAD_CMD="systemctl reload uzenet-room"  # or restart, or leave empty

# Function: Check if cert will expire in < 14 days
is_cert_expiring() {
	if [ ! -f "$CERT_PATH" ]; then
		echo "No existing certificate found."
		return 0
	fi
	expiry_date=$(openssl x509 -enddate -noout -in "$CERT_PATH" | cut -d= -f2)
	expiry_seconds=$(date -d "$expiry_date" +%s)
	now_seconds=$(date +%s)
	let "days_left = ($expiry_seconds - $now_seconds) / 86400"
	[[ "$days_left" -lt 14 ]]
}

# Function: Renew cert
renew_cert() {
	echo "[*] Attempting renewal for $DOMAIN"
	certbot certonly --standalone --non-interactive --agree-tos \
		--preferred-challenges http \
		--email "$EMAIL" \
		-d "$DOMAIN"
}

# Main logic
if is_cert_expiring; then
	echo "[!] Certificate is missing or expiring soon. Renewing..."
	if renew_cert; then
		echo "[+] Certificate renewed successfully."
		if [ -n "$SERVER_RELOAD_CMD" ]; then
			echo "[*] Reloading server..."
			$SERVER_RELOAD_CMD
		fi
	else
		echo "[!] Certificate renewal failed!"
		exit 1
	fi
else
	echo "[✓] Certificate is valid and not expiring soon."
fi
