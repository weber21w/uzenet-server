#!/usr/bin/env bash
# status-uzenet-virtual-fujinet.sh
# Show status and recent logs for the uzenet-virtual-fujinet service.

set -euo pipefail

SERVICE_NAME="uzenet-virtual-fujinet"
SYSTEMD_UNIT="${SERVICE_NAME}.service"
LOG_IDENTIFIER="${SERVICE_NAME}"

echo "==> Service status: ${SYSTEMD_UNIT}"
echo

if ! systemctl list-unit-files | grep -q "^${SYSTEMD_UNIT}"; then
	echo "Service ${SYSTEMD_UNIT} is not installed (no unit file found)."
	exit 1
fi

systemctl status "${SYSTEMD_UNIT}" --no-pager || true

echo
echo "==> Recent logs (last 50 lines) for ${LOG_IDENTIFIER}"
echo

# Try journalctl first
if command -v journalctl >/dev/null 2>&1; then
	journalctl -u "${SYSTEMD_UNIT}" -n 50 --no-pager || true
else
	# Fallback to syslog if journalctl is unavailable
	if [ -f /var/log/syslog ]; then
		grep "${LOG_IDENTIFIER}" /var/log/syslog | tail -n 50 || true
	elif [ -f /var/log/messages ]; then
		grep "${LOG_IDENTIFIER}" /var/log/messages | tail -n 50 || true
	else
		echo "No journalctl and no /var/log/syslog or /var/log/messages found."
	fi
fi

echo
echo "==> Done."
