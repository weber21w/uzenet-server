#!/usr/bin/env bash
set -euo pipefail

SERVICE_NAME="uzenet-irc"
EXEC_NAME="uzenet-irc-server"
SERVICE_USER="uzenet"
SERVICE_GROUP="uzenet"
SERVICE_ROOT="/var/lib/uzenet-irc"
INSTALL_PATH="/usr/local/bin/${EXEC_NAME}"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
RSYSLOG_CONF="/etc/rsyslog.d/${SERVICE_NAME}.conf"
LOG_FILE="/var/log/${SERVICE_NAME}.log"
LOGROTATE_CONF="/etc/logrotate.d/${SERVICE_NAME}"
DEFAULT_PORT=57431
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "[0/9] Verifying root access..."
if [[ $EUID -ne 0 ]]; then
	echo "Please run this script with sudo."
	exit 1
fi

echo "[1/9] Building server..."
make clean
make

echo "[2/9] Creating service user '${SERVICE_USER}' if needed..."
if ! id "${SERVICE_USER}" &>/dev/null; then
	useradd -r -s /usr/sbin/nologin -d "${SERVICE_ROOT}" "${SERVICE_USER}"
fi

echo "[3/9] Installing binary to ${INSTALL_PATH}..."
install -Dm 755 "${EXEC_NAME}" "${INSTALL_PATH}"

echo "[4/9] Preparing data directory at ${SERVICE_ROOT}..."
install -d -o "${SERVICE_USER}" -g "${SERVICE_GROUP}" -m 750 "${SERVICE_ROOT}"

echo "[5/9] Writing systemd service to ${SERVICE_FILE}..."
cat > "${SERVICE_FILE}" <<EOF
[Unit]
Description=Uzenet IRC Proxy Server
After=network.target

[Service]
Type=simple
ExecStart=${INSTALL_PATH} --listen-port ${DEFAULT_PORT}
WorkingDirectory=${SERVICE_ROOT}
User=${SERVICE_USER}
Group=${SERVICE_GROUP}
Restart=on-failure
ProtectSystem=full
ReadWritePaths=${SERVICE_ROOT}
StandardOutput=syslog
StandardError=syslog
SyslogIdentifier=${SERVICE_NAME}

[Install]
WantedBy=multi-user.target
EOF

echo "[6/9] Installing rsyslog and logrotate config..."
install -m 644 "${SCRIPT_DIR}/uzenet-irc.conf.rsyslog" "${RSYSLOG_CONF}"
install -m 644 "${SCRIPT_DIR}/uzenet-irc.conf.logrotate" "${LOGROTATE_CONF}"
touch "${LOG_FILE}"
chown "${SERVICE_USER}:${SERVICE_GROUP}" "${LOG_FILE}"
chmod 644 "${LOG_FILE}"

echo "[7/9] Running setup scripts if present..."
if [[ -x "${SCRIPT_DIR}/setup-fail2ban.sh" ]]; then
	echo "↪ Running setup-fail2ban.sh..."
	"${SCRIPT_DIR}/setup-fail2ban.sh"
fi

echo "[8/9] Enabling and starting service..."
systemctl daemon-reload
systemctl enable --now "${SERVICE_NAME}.service"
systemctl restart rsyslog

echo "✅ Installed and started ${SERVICE_NAME}."
echo "📄 Logs: ${LOG_FILE}"
systemctl status "${SERVICE_NAME}.service" --no-pager
