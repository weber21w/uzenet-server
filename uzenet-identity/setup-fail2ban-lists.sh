#!/usr/bin/env bash
set -euo pipefail

# Must run as root
if [[ $(id -u) -ne 0 ]]; then
  echo "Error: this script must be run as root." >&2
  exit 1
fi

# Paths
LOGDIR=/var/log/uzenet
LOGFILE=$LOGDIR/identity.log
PERM_ALLOW=/etc/fail2ban/allowlist.perm
TMP_ALLOW=/etc/fail2ban/allowlist.tmp
FILTER_FILE=/etc/fail2ban/filter.d/uzenet-identity.conf
JAIL_FILE=/etc/fail2ban/jail.d/iden-all.conf

# 1) Install Fail2Ban
echo "→ Installing Fail2Ban..."
apt update
DEBIAN_FRONTEND=noninteractive apt install -y fail2ban

# 2) Prepare log directory and file
echo "→ Setting up identity log..."
mkdir -p "$LOGDIR"
touch "$LOGFILE"
chown syslog:adm "$LOGFILE"
chmod 644 "$LOGFILE"

# 3) Create allowlists
echo "→ Creating allowlists..."
touch "$PERM_ALLOW" "$TMP_ALLOW"
chmod 644 "$PERM_ALLOW" "$TMP_ALLOW"

# 4) Write Fail2Ban filter for identity auth-fail
echo "→ Writing filter: $FILTER_FILE"
cat > "$FILTER_FILE" << 'EOF'
[Definition]
# Matches ISO8601 UTC timestamp + identity auth-fail logs
definition
failregex = ^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z IDENTITY auth-fail service=\S+ ip=<HOST> user=\S+$
ignoreregex =
EOF

# 5) Configure unified jail
echo "→ Writing jail: $JAIL_FILE"
cat > "$JAIL_FILE" << EOF
[iden-all]
enabled   = true
banaction = iptables-allports
port      = n/a
filter    = uzenet-identity
logpath   = $LOGFILE
maxretry  = 1
bantime   = 86400    ; 24 hours\ignoreip  = 127.0.0.1/8 ::1
ignoreip  = $PERM_ALLOW
ignoreip  = $TMP_ALLOW
EOF

# 6) Enable & reload Fail2Ban
echo "→ Enabling and restarting Fail2Ban..."
systemctl enable fail2ban
systemctl restart fail2ban

echo "✅ Fail2Ban identity jail is active."

# Reminder for service logging
cat << NOTE

Reminder: Ensure all Uzenet services log auth failures in this exact format to $LOGFILE:
  YYYY-MM-DDTHH:MM:SSZ IDENTITY auth-fail service=<name> ip=<IP> user=<username>

NOTE
