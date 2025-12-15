#!/bin/bash
set -e

BASE_DIR="$(dirname "$0")"
CERT_UPDATE_SCRIPT="$BASE_DIR/uzenet-room/update-cert.sh"

echo "[INFO] Starting Uzenet maintenance: $(date)"

# --- TLS Certificate Renewal ---
if [ -x "$CERT_UPDATE_SCRIPT" ]; then
	echo "[INFO] Checking for TLS cert update..."
	"$CERT_UPDATE_SCRIPT"
else
	echo "[WARN] TLS update script not found or not executable: $CERT_UPDATE_SCRIPT"
fi

# --- Add more tasks below this line ---
# echo "[INFO] Running database vacuum..."
# ./vacuum-users.sh

echo "[INFO] Maintenance complete."
