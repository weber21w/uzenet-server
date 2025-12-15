#!/usr/bin/env bash
set -euo pipefail

echo "📋 Uzenet Identity service status:"
echo

# Systemd status
if systemctl list-unit-files | grep -q uzenet-identity.service; then
	systemctl status uzenet-identity.service --no-pager
else
	echo "⚠️  Service unit not found: uzenet-identity.service"
fi

echo
# Binary check
if [[ -f /usr/local/bin/uzenet-identity ]]; then
	echo "✅ Binary exists at /usr/local/bin/uzenet-identity"
else
	echo "❌ Binary not found at /usr/local/bin/uzenet-identity"
fi

echo
# Sidecar dir check
SIDECAR_BASE="/var/lib/uzenet/sidecars"
if [[ -d "$SIDECAR_BASE" ]]; then
	echo "✅ Sidecar directory exists: $SIDECAR_BASE"
else
	echo "⚠️  Sidecar directory missing: $SIDECAR_BASE"
fi

echo
# Fail2Ban jail check
if command -v fail2ban-client >/dev/null 2>&1; then
	echo "🛡️  Fail2Ban status:"
	fail2ban-client status uzenet-identity 2>/dev/null || echo "⚠️  Jail 'uzenet-identity' not found"
else
	echo "❌ Fail2Ban not installed or not in PATH"
fi
