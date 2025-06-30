#!/bin/bash
set -e

UZN_DIR="/opt/uzenet-master"

echo "=== Uzenet Updater ==="
echo "[*] Changing to directory: $UZN_DIR"
cd "$UZN_DIR"

echo "[*] Pulling latest changes from GitHub..."
git pull --rebase

echo "[*] Converting line endings to Unix style..."
find . -type f -exec dos2unix {} +

echo "[*] Rebuilding entire Uzenet stack..."
chmod +x make-all.sh
./make-all.sh

echo "[✓] Uzenet stack updated and rebuilt."
