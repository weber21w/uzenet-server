#!/usr/bin/env bash
set -euo pipefail

### Check for root ###
if [ "$(id -u)" -ne 0 ]; then
	echo "This script must be run as root." >&2
	exit 1
fi

### Parse args ###
if [ "$#" -lt 2 ] || [ "$#" -gt 4 ]; then
	cat <<EOF
Usage: sudo $0 <OperName> <Password> [HostMask] [Flags]

  <OperName>   IRC operator nickname (no spaces)
  <Password>   Plaintext password to hash
  [HostMask]   Who this operator may connect from (default "*@*")
  [Flags]      Operator privileges (default "oOzZ")

Example:
  sudo $0 alice hunter2 '*@192.0.2.*' 'oO'
EOF
	exit 1
fi

OPER_NAME="$1"
OPER_PASS="$2"
HOST_MASK="${3:-*@*}"
FLAGS="${4:-oOzZ}"
CONF="/etc/ngircd/ngircd.conf"

### Generate hashed password ###
if command -v ngircd >/dev/null 2>&1; then
	HASHED="$(ngircd --hash-password "$OPER_PASS")"
elif command -v openssl >/dev/null 2>&1; then
	hex="$(printf "%s" "$OPER_PASS" | openssl dgst -sha256 -hex | sed 's/^.* //')"
	HASHED="{SHA256}$hex"
else
	echo "Error: cannot hash password — install ngircd or openssl." >&2
	exit 1
fi

### Append Operator block ###
cat >> "$CONF" <<EOF

[Operator]
Name     = $OPER_NAME
Password = "$HASHED"
Host     = $HOST_MASK
Flags    = "$FLAGS"
EOF

echo "→ Appended [Operator] '$OPER_NAME' to $CONF"

### Reload ngIRCd ###
systemctl reload ngircd
echo "→ Reloaded ngIRCd; operator '$OPER_NAME' is now active."
