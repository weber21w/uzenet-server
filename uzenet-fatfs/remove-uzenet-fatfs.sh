#!/usr/bin/env bash
set -euo pipefail

SERVICE_NAME="uzenet-fatfs"
EXEC_NAME="uzenet-fatfs-server"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
RSYSLOG_CONF="/etc/rsyslog.d/${SERVICE_NAME}.conf"
LOGROTATE_CONF="/etc/logrotate.d/${SERVICE_NAME}.conf"
INSTALL_PATH="/usr/local/bin/${EXEC_NAME}"
LOG_FILE="/var/log/${SERVICE_NAME}.log"

echo "[1/6] Stopping and disabling systemd service…"
systemctl stop "${SERVICE_NAME}.service" 2>/dev/null || true
systemctl disable "${SERVICE_NAME}.service" 2>/dev/null || true
systemctl daemon-reload

echo "[2/6] Removing systemd unit file…"
rm -f "${SERVICE_FILE}"

echo "[3/6] Removing binary…"
rm -f "${INSTALL_PATH}"

echo "[4/6] Removing rsyslog config…"
rm -f "${RSYSLOG_CONF}"
systemctl restart rsyslog

echo "[5/6] Removing logrotate rule…"
rm -f "${LOGROTATE_CONF}"

echo "[6/6] (Optional) Remove logs?"
read -rp "Delete ${LOG_FILE} and all rotated logs? [y/N] " resp
if [[ "${resp}" =~ ^[Yy]$ ]]; then
    rm -f "${LOG_FILE}"*
    echo "Logs removed."
else
    echo "Logs preserved."
fi

echo "✅ Uninstall complete."
