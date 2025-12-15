#!/usr/bin/env bash
# install-uzenet-virtual-fujinet.sh
# Self-contained installer for the uzenet-virtual-fujinet service.

set -euo pipefail

SERVICE_NAME="uzenet-virtual-fujinet"
BINARY_NAME="uzenet-virtual-fujinet"
SERVICE_USER="uzenet"
SERVICE_GROUP="uzenet"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR"
INSTALL_BIN="/usr/local/sbin/${BINARY_NAME}"
SYSTEMD_UNIT="/etc/systemd/system/${SERVICE_NAME}.service"

LOG_DIR="/var/log/uzenet"
STATE_DIR="/var/lib/uzenet"

echo "==> Installing ${SERVICE_NAME} from ${BUILD_DIR}"

#---------------------------------------------------------------------
# Ensure service user/group
#---------------------------------------------------------------------
if ! id -u "${SERVICE_USER}" >/dev/null 2>&1; then
	echo "==> Creating system user ${SERVICE_USER}"
	useradd --system --home "${STATE_DIR}" --shell /usr/sbin/nologin "${SERVICE_USER}" || true
fi

# Ensure group exists (in case useradd didn't create it)
if ! getent group "${SERVICE_GROUP}" >/dev/null 2>&1; then
	echo "==> Creating group ${SERVICE_GROUP}"
	groupadd --system "${SERVICE_GROUP}" || true
fi

#---------------------------------------------------------------------
# Build binary (expects a Makefile in BUILD_DIR)
#---------------------------------------------------------------------
if [ -f "${BUILD_DIR}/Makefile" ]; then
	echo "==> Building ${BINARY_NAME}"
	make -C "${BUILD_DIR}" clean
	make -C "${BUILD_DIR}" "${BINARY_NAME}"
else
	echo "ERROR: No Makefile found in ${BUILD_DIR}"
	exit 1
fi

#---------------------------------------------------------------------
# Install binary
#---------------------------------------------------------------------
echo "==> Installing binary to ${INSTALL_BIN}"
install -D -m 0755 "${BUILD_DIR}/${BINARY_NAME}" "${INSTALL_BIN}"
chown "${SERVICE_USER}:${SERVICE_GROUP}" "${INSTALL_BIN}"

#---------------------------------------------------------------------
# Directories for logs and state
#---------------------------------------------------------------------
echo "==> Ensuring log and state directories exist"
mkdir -p "${LOG_DIR}"
mkdir -p "${STATE_DIR}"

chown -R "${SERVICE_USER}:${SERVICE_GROUP}" "${LOG_DIR}" "${STATE_DIR}"
chmod 0755 "${STATE_DIR}"
chmod 0755 "${LOG_DIR}"

#---------------------------------------------------------------------
# Systemd service unit
#---------------------------------------------------------------------
echo "==> Writing systemd unit ${SYSTEMD_UNIT}"

cat > "${SYSTEMD_UNIT}" <<EOF
[Unit]
Description=Uzenet Virtual FujiNet service
After=network-online.target uzenet-room.service
Wants=network-online.target

[Service]
Type=simple
User=${SERVICE_USER}
Group=${SERVICE_GROUP}
WorkingDirectory=${STATE_DIR}
ExecStart=${INSTALL_BIN}
Restart=on-failure
RestartSec=2

# Use syslog for logs (fail2ban-friendly)
StandardOutput=syslog
StandardError=syslog
SyslogIdentifier=${SERVICE_NAME}

# Give it some reasonable limits
LimitNOFILE=16384

[Install]
WantedBy=multi-user.target
EOF

chmod 0644 "${SYSTEMD_UNIT}"

#---------------------------------------------------------------------
# Optional per-service setup script
#---------------------------------------------------------------------
SETUP_SCRIPT="${SCRIPT_DIR}/setup-${SERVICE_NAME}.sh"
if [ -x "${SETUP_SCRIPT}" ]; then
	echo "==> Running optional setup script ${SETUP_SCRIPT}"
	"${SETUP_SCRIPT}"
fi

#---------------------------------------------------------------------
# Reload systemd and enable/start service
#---------------------------------------------------------------------
echo "==> Reloading systemd units"
systemctl daemon-reload

echo "==> Enabling ${SERVICE_NAME}"
systemctl enable "${SERVICE_NAME}"

echo "==> Restarting ${SERVICE_NAME}"
systemctl restart "${SERVICE_NAME}"

echo "==> ${SERVICE_NAME} installation complete."
