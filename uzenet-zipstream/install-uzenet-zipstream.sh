#!/usr/bin/env bash
set -euo pipefail

SERVICE_NAME="uzenet-zipstream"
EXEC_NAME="uzenet-zipstream-server"
SERVICE_USER="uzenet"
SERVICE_ROOT="/var/lib/uzenet-zipstream"
INSTALL_PATH="/usr/local/bin/${EXEC_NAME}"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
RSYSLOG_CONF="/etc/rsyslog.d/${SERVICE_NAME}.conf"
LOG_FILE="/var/log/${SERVICE_NAME}.log"
LOGROTATE_CONF="/etc/logrotate.d/${SERVICE_NAME}.conf"

echo "[0/8] Verifying root access…"
if [[ $EUID -ne 0 ]]; then
    echo "⚠️  Please run this script with sudo."
    exit 1
fi

echo "[1/8] Building server…"
make clean
make

echo "[2/8] Creating service user '${SERVICE_USER}' if needed…"
if ! id "${SERVICE_USER}" &>/dev/null; then
    useradd -r -s /usr/sbin/nologin -d "${SERVICE_ROOT}" "${SERVICE_USER}"
fi

echo "[3/8] Installing binary to ${INSTALL_PATH}…"
install -Dm 755 "${EXEC_NAME}" "${INSTALL_PATH}"

echo "[4/8] Preparing data directory at ${SERVICE_ROOT}…"
install -d -o "${SERVICE_USER}" -g "${SERVICE_USER}" -m 750 "${SERVICE_ROOT}"
install -d -o "${SERVICE_USER}" -g "${SERVICE_USER}" -m 750 "${SERVICE_ROOT}/uzenetfs-guest"

echo "[5/8] Writing systemd service file to ${SERVICE_FILE}…"
cat > "${SERVICE_FILE}" <<EOF
[Unit]
Description=Uzenet Zipstream Compressed File Server
After=network.target

[Service]
Type=simple
ExecStart=${INSTALL_PATH}
WorkingDirectory=${SERVICE_ROOT}
User=${SERVICE_USER}
Group=${SERVICE_USER}
Restart=on-failure
ProtectSystem=full
ReadWritePaths=${SERVICE_ROOT}
StandardOutput=syslog
StandardError=syslog
SyslogIdentifier=${SERVICE_NAME}

[Install]
WantedBy=multi-user.target
EOF

echo "[6/8] Configuring rsyslog to send '${SERVICE_NAME}' logs to ${LOG_FILE}…"
cat > "${RSYSLOG_CONF}" <<EOF
if \$programname == '${SERVICE_NAME}' then ${LOG_FILE}
& stop
EOF

echo "[7/8] Setting up logrotate for ${LOG_FILE}…"
cat > "${LOGROTATE_CONF}" <<EOF
${LOG_FILE} {
    daily
    rotate 7
    compress
    delaycompress
    missingok
    notifempty
    create 0640 ${SERVICE_USER} adm
    sharedscripts
    postrotate
        systemctl kill --signal=HUP rsyslog.service >/dev/null 2>&1 || true
    endscript
}
EOF

echo "[8/8] Reloading daemons and starting service…"
systemctl daemon-reload
systemctl enable --now "${SERVICE_NAME}.service"
systemctl restart rsyslog

echo "✅ Installed and started ${SERVICE_NAME}. Logs: ${LOG_FILE}"
systemctl status "${SERVICE_NAME}.service" --no-pager
