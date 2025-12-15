#!/usr/bin/env bash
set -euo pipefail

SERVICE_NAME="uzenet-fatfs"
EXEC_NAME="uzenet-fatfs-server"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
RSYSLOG_CONF="/etc/rsyslog.d/${SERVICE_NAME}.conf"
LOGROTATE_CONF="/etc/logrotate.d/${SERVICE_NAME}.conf"
INSTALL_PATH="/usr/local/bin/${EXEC_NAME}"
LOG_FILE="/var/log/${SERVICE_NAME}.log"

echo "🔨 Building ${EXEC_NAME}..."
make clean && make

echo "[1/6] Installing binary to ${INSTALL_PATH}…"
sudo cp "${EXEC_NAME}" "${INSTALL_PATH}"
sudo chmod +x "${INSTALL_PATH}"

echo "[2/6] Creating systemd unit…"
sudo tee "${SERVICE_FILE}" > /dev/null <<EOF
[Unit]
Description=Uzenet ${SERVICE_NAME} service
After=network.target

[Service]
ExecStart=${INSTALL_PATH}
Restart=on-failure

[Install]
WantedBy=multi-user.target
EOF

echo "[3/6] Creating rsyslog config…"
sudo tee "${RSYSLOG_CONF}" > /dev/null <<EOF
if \$programname == '${EXEC_NAME}' then ${LOG_FILE}
& stop
EOF

echo "[4/6] Creating logrotate config…"
sudo tee "${LOGROTATE_CONF}" > /dev/null <<EOF
${LOG_FILE} {
    weekly
    rotate 4
    compress
    missingok
    notifempty
    copytruncate
}
EOF

echo "[5/6] Reloading daemons…"
sudo systemctl daemon-reload
sudo systemctl enable "${SERVICE_NAME}.service"
sudo systemctl restart "${SERVICE_NAME}.service"
sudo systemctl restart rsyslog

echo "[6/6] ✅ Install complete for ${SERVICE_NAME}"
