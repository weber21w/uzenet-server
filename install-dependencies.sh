#!/bin/bash
set -e

echo "[*] Detecting distro..."

if [ -f /etc/os-release ]; then
	. /etc/os-release
	distro=$ID
else
	echo "[!] Cannot detect OS. Please install dependencies manually."
	exit 1
fi

echo "[*] Detected OS: $distro"

if [[ "$distro" == "ubuntu" || "$distro" == "debian" ]]; then
	echo "[*] Installing dependencies via apt..."
	sudo apt update
	xargs -a dependencies.txt sudo apt install -y
elif [[ "$distro" == "arch" ]]; then
	echo "[*] Installing dependencies via pacman..."
	sudo pacman -Sy --noconfirm
	xargs -a dependencies.txt sudo pacman -S --noconfirm
else
	echo "[!] Unsupported distro: $distro"
	exit 1
fi

echo "[✓] All dependencies installed."
