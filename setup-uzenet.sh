#!/bin/bash
set -e

echo "=== Uzenet Installer ==="
echo "[*] Updating package lists..."
sudo apt update

echo "[*] Installing required packages..."
sudo apt install -y git build-essential libmicrohttpd-dev libssl-dev libevent-dev

echo "[*] Cloning Uzenet repository..."
git clone https://github.com/YOUR_USERNAME/uzenet-master.git /opt/uzenet-master
cd /opt/uzenet-master

echo "[*] Converting all files to Unix line endings..."
find . -type f -exec dos2unix {} +

echo "[*] Making everything..."
chmod +x make-all.sh
./make-all.sh --install-deps

echo "[✓] Uzenet stack installed successfully in /opt/uzenet-master"

