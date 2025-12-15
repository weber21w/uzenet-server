#!/usr/bin/env bash
set -euo pipefail

# UzeNet Web installer (Ubuntu)
# - Serves static site via Caddy
# - Runs Flask API via Gunicorn (systemd)
#
# Repo root must contain:
#   uzenet-site/
#   uzenet-api/
#   uzenet-emscripten-bridge/ (unused for now)
#
# Optional:
#   export UZENET_DOMAIN="uzenet.example.com"   # default ":80" (HTTP only)
#
# Usage:
#   ./install-uzenet-web.sh
#   UZENET_DOMAIN="uzenet.example.com" ./install-uzenet-web.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$SCRIPT_DIR"

SITE_DIR="$REPO_ROOT/uzenet-site"
API_DIR="$REPO_ROOT/uzenet-api"

DEPLOY_ROOT="/opt/uzenet"
DEPLOY_SITE="$DEPLOY_ROOT/uzenet-site"
DEPLOY_API="$DEPLOY_ROOT/uzenet-api"

SERVICE_USER="uzenet"
SERVICE_GROUP="uzenet"

# Domain: use env var if provided, otherwise prompt.
if [[ -z "${UZENET_DOMAIN:-}" ]]; then
	echo
	echo "UZENET_DOMAIN is not set."
	echo "Enter a domain (recommended) for automatic HTTPS, or leave blank for HTTP on :80."
	read -r -p "UZENET_DOMAIN: " DOMAIN
	if [[ -z "$DOMAIN" ]]; then
		DOMAIN=":80"
	fi
else
	DOMAIN="$UZENET_DOMAIN"
fi

die(){
	echo "ERROR: $*" >&2
	exit 1
}

need(){
	command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

if [[ ! -d "$SITE_DIR" ]]; then die "missing directory: $SITE_DIR"; fi
if [[ ! -d "$API_DIR" ]]; then die "missing directory: $API_DIR"; fi
if [[ ! -f "$API_DIR/app.py" ]]; then die "missing: $API_DIR/app.py"; fi
if [[ ! -f "$API_DIR/requirements.txt" ]]; then die "missing: $API_DIR/requirements.txt"; fi
if [[ ! -f "$API_DIR/gunicorn.conf.py" ]]; then
	echo "WARN: missing $API_DIR/gunicorn.conf.py (service will still run with defaults if you add -b in ExecStart)"
fi

need sudo
need rsync

echo "Installing system packages..."
sudo apt update
sudo apt install -y \
	ca-certificates \
	curl \
	gnupg \
	rsync \
	python3 \
	python3-venv \
	python3-pip \
	debian-keyring \
	debian-archive-keyring \
	apt-transport-https

echo "Installing Caddy (official repo)..."
# Based on official Caddy Debian/Ubuntu install steps
if [[ ! -f /etc/apt/sources.list.d/caddy-stable.list ]]; then
	sudo mkdir -p /usr/share/keyrings
	curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' | sudo gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
	curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt' | sudo tee /etc/apt/sources.list.d/caddy-stable.list >/dev/null
	sudo chmod o+r /usr/share/keyrings/caddy-stable-archive-keyring.gpg
	sudo chmod o+r /etc/apt/sources.list.d/caddy-stable.list
	sudo apt update
fi
sudo apt install -y caddy

echo "Ensuring service user exists..."
if ! id "$SERVICE_USER" >/dev/null 2>&1; then
	sudo useradd --system --home /var/lib/uzenet --create-home --shell /usr/sbin/nologin "$SERVICE_USER"
fi

echo "Deploying repo to $DEPLOY_ROOT ..."
sudo mkdir -p "$DEPLOY_SITE" "$DEPLOY_API"
sudo rsync -a --delete "$SITE_DIR/" "$DEPLOY_SITE/"
sudo rsync -a --delete "$API_DIR/" "$DEPLOY_API/"

# Permissions:
# - Caddy runs as user "caddy" (from package) and must read the static site.
# - Gunicorn runs as uzenet user and must own its venv + api dir.
sudo chmod -R a+rX "$DEPLOY_SITE"
sudo chown -R "$SERVICE_USER:$SERVICE_GROUP" "$DEPLOY_API"

echo "Setting up Python venv + deps..."
sudo -u "$SERVICE_USER" bash -lc "
	set -euo pipefail
	cd '$DEPLOY_API'
	python3 -m venv .venv
	./.venv/bin/pip install --upgrade pip
	./.venv/bin/pip install -r requirements.txt
"

echo "Writing systemd service: /etc/systemd/system/uzenet-api.service"
sudo tee /etc/systemd/system/uzenet-api.service >/dev/null <<EOF
[Unit]
Description=UzeNet API (Flask via Gunicorn)
After=network.target

[Service]
Type=simple
User=$SERVICE_USER
Group=$SERVICE_GROUP
WorkingDirectory=$DEPLOY_API
Environment=PYTHONUNBUFFERED=1
Environment=PATH=$DEPLOY_API/.venv/bin
ExecStart=$DEPLOY_API/.venv/bin/gunicorn -c gunicorn.conf.py app:app
Restart=on-failure
RestartSec=2
TimeoutStopSec=5
KillSignal=SIGINT

[Install]
WantedBy=multi-user.target
EOF

echo "Writing Caddyfile: /etc/caddy/Caddyfile"
sudo cp -f /etc/caddy/Caddyfile /etc/caddy/Caddyfile.bak 2>/dev/null || true
sudo tee /etc/caddy/Caddyfile >/dev/null <<EOF
$DOMAIN {

	encode zstd gzip

	# API (Flask/Gunicorn)
	handle /api/* {
		reverse_proxy 127.0.0.1:5000
	}

	# Static site
	handle {
		root * $DEPLOY_SITE
		file_server
	}
}
EOF

echo "Reloading services..."
sudo systemctl daemon-reload
sudo systemctl enable --now uzenet-api.service
sudo systemctl enable --now caddy

sudo systemctl restart uzenet-api.service
sudo systemctl reload caddy || sudo systemctl restart caddy

echo
echo "Done."
echo "Site root: $DEPLOY_SITE"
echo "API root : $DEPLOY_API"
echo
echo "Quick checks:"
echo "  curl -s http://127.0.0.1:5000/api/health || true"
echo "  curl -s http://127.0.0.1/api/health || true"
echo
echo "Forums link should point to: https://uzebox.org/forums/  (not .orf)"
