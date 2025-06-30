#!/usr/bin/env bash
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
  echo "Error: must run as root." >&2
  exit 1
fi

echo "→ Installing uzenet-sim-server binary…"
install -m 755 uzenet-sim /usr/local/bin/uzenet-sim

echo "→ Setting up IPC directory…"
install -d -m 755 /run/uzenet-sim-server
chown www-data:www-data /run/uzenet-sim-server

echo "→ Writing systemd template…"
cat > /etc/systemd/system/uzenet-sim-server@.service <<'EOF'
[Unit]
Description=UZENET Simulator for %i
After=network.target

[Service]
ExecStart=/usr/local/bin/uzenet-sim-server --game=%i --ipc=/run/uzenet-sim-server/%i.sock
Restart=on-failure
User=www-data
Group=www-data

[Install]
WantedBy=multi-user.target
EOF

echo "→ Reloading systemd daemon…"
systemctl daemon-reload

echo "✅ Installed. To start a simulator instance for GAMECODE, run:"
echo "    sudo systemctl enable --now uzenet-sim-server@MEGATR00"
