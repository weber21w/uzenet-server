#!/usr/bin/env bash
# remove-uzenet-virtual-fujinet.sh
# Uninstall the uzenet-virtual-fujinet service.
#
# Default: keep logs and state (for forensics/history).
# Use --purge to also remove logs, state dir, and service user.

set -euo pipefail

SERVICE_NAME="uzenet-virtual-fujinet"
BINARY_NAME="uzenet-virtual-fujinet"
SERVICE_USER="uzenet"
SERVICE_GROUP="uzenet"

INSTALL_BIN="/usr/local/sbin/${BINARY_NAME}"
SYSTEMD_UNIT="/etc/systemd/system/${SERVICE_NAME}.service"

LOG_DIR="/var/log/uzenet"
STATE_DIR="/var/lib/uzenet"

PURGE=0

if [[ "${1-}" == "--purge" ]]; then
	PURGE=1
fi

echo "==> Removing ${SERVICE_NAME} (purge=${PURGE})"

#---------------------------------------------------------------------
# Stop and disable systemd service if present
#---------------------------------------------------------------------
if systemctl list-unit-files | grep -q "^${SERVICE_NAME}.service"; then
	echo "==> Stopping service ${SERVICE_NAME}"
	systemctl stop "${SERVICE_NAME}" || true

	echo "==> Disabling service ${SERVICE_NAME}"
	systemctl disable "${SERVICE_NAME}" || true
fi

#---------------------------------------------------------------------
# Remove systemd unit
#---------------------------------------------------------------------
if [[ -f "${SYSTEMD_UNIT}" ]]; then
	echo "==> Removing systemd unit ${SYSTEMD_UNIT}"
	rm -f "${SYSTEMD_UNIT}"
	systemctl daemon-reload || true
fi

#---------------------------------------------------------------------
# Remove installed binary
#---------------------------------------------------------------------
if [[ -x "${INSTALL_BIN}" || -f "${INSTALL_BIN}" ]]; then
	echo "==> Removing binary ${INSTALL_BIN}"
	rm -f "${INSTALL_BIN}"
fi

#---------------------------------------------------------------------
# Optionally purge logs and state
#---------------------------------------------------------------------
if [[ "${PURGE}" -eq 1 ]]; then
	echo "==> Purging state and logs for ${SERVICE_NAME}"

	if [[ -d "${STATE_DIR}" ]]; then
		echo "   - Removing STATE_DIR=${STATE_DIR}"
		rm -rf "${STATE_DIR}"
	fi

	if [[ -d "${LOG_DIR}" ]]; then
		echo "   - Removing LOG_DIR=${LOG_DIR}"
		rm -rf "${LOG_DIR}"
	fi

	# Optionally remove user/group if no longer used
	if id -u "${SERVICE_USER}" >/dev/null 2>&1; then
		echo "   - Removing user ${SERVICE_USER}"
		userdel "${SERVICE_USER}" || true
	fi
	if getent group "${SERVICE_GROUP}" >/dev/null 2>&1; then
		echo "   - Removing group ${SERVICE_GROUP}"
		groupdel "${SERVICE_GROUP}" || true
	fi
else
	echo "==> Leaving logs in ${LOG_DIR} and state in ${STATE_DIR} (no purge)"
fi

echo "==> ${SERVICE_NAME} removal complete."
