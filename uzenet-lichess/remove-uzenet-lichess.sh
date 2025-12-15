#!/usr/bin/env bash
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
	echo "Must run as root." >&2
	exit 1
fi

echo "→ Installing build dependencies..."
apt update
DEBIAN_FRONTEND=noninteractive apt install -y libcurl4-openssl-dev

echo "→ Ensuring uzenet user exists..."
if ! id -u uzenet >/dev/null 2>&1; then
	useradd --system --home /var/lib/uzenet --shell /usr/sbin/nologin uzenet
fi

echo "→ Building uzenet-lichess..."
cd "$(dirname "$0")"
make clean || true
make

echo "→ Installing binary..."
install -m 755 uzenet-lichess /usr/local/bin/uzenet-lichess

echo "→ Creating runtime directories..."
mkdir -p /run/uzenet
chown uzenet:uzenet /run/uzenet
chmod 755 /run/uzenet

mkdir -p /var/lib/uzenet/lichess-users
chown -R uzenet:uzenet /var/lib/uzenet
chmod 750 /var/lib/uzenet/lichess-users

echo "→ Creating config directory and env file..."
mkdir -p /etc/uzenet
if [ ! -f /etc/uzenet/lichess.env ]; then
	cat > /etc/uzenet/lichess.env <<EOF
# Environment for uzenet-lichess
# Per-user tokens are loaded from /var/lib/uzenet/lichess-users/<user_id>.json
# This shared token is used as a fallback if a user has no specific token file.
#
# Obtain a personal API token from: https://lichess.org/account/oauth/token
# and assign it here:
#
#LICHESS_SHARED_TOKEN="your_lichess_api_token_here"
EOF
	chown uzenet:uzenet /etc/uzenet/lichess.env
	chmod 640 /etc/uzenet/lichess.env
fi

echo "→ Writing systemd service unit..."
cat > /etc/systemd/system/uzenet-lichess.service <<EOF
[Unit]
Description=UzeNet Lichess Proxy Service
After=network.target

[Service]
ExecStart=/usr/local/bin/uzenet-lichess
User=uzenet
Group=uzenet
EnvironmentFile=-/etc/uzenet/lichess.env
Restart=on-failure
RuntimeDirectory=uzenet
RuntimeDirectoryMode=0755

[Install]
WantedBy=multi-user.target
EOF

echo "→ Running any local setup scripts..."
# Allow optional per-service setup helpers, but don't fail if none exist
shopt -s nullglob
for script in "$(dirname "$0")"/setup-uzenet-lichess-*; do
	if [ -x "\$script" ]; then
		echo "→ Running \${script##*/}..."
		"\$script"
	fi
done
shopt -u nullglob

echo "→ Enabling and starting service..."
systemctl daemon-reload
systemctl enable uzenet-lichess
systemctl restart uzenet-lichess

echo "uzenet-lichess installed and running."
