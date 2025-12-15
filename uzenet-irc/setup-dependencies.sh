#!/usr/bin/env bash
# install-dependencies.sh
# Installs build dependencies for uzenet-irc-server on Ubuntu/Debian

set -euo pipefail

# Must be root
if [[ $EUID -ne 0 ]]; then
  echo "⚠️  Please run this script with sudo or as root."
  exit 1
fi

echo "[1/3] Updating package lists..."
apt-get update

echo "[2/3] Installing build tools and dev libraries..."
apt-get install -y \
  build-essential \
  pkg-config \
  libevent-dev \
  libssl-dev

echo "[3/3] Verifying required headers..."
if [[ -f /usr/include/event2/bufferevent_ssl.h ]]; then
  echo "✔ bufferevent_ssl header found"
else
  echo "❌ bufferevent_ssl header NOT found; ensure libevent-dev is installed"
  exit 1
fi

echo "✅ All dependencies installed."
echo
echo "Now update your Makefile LDFLAGS to include:"
echo "    -levent -levent_openssl -lssl -lcrypto"
echo "and then run:"
echo "    make clean && make"
