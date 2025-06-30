#!/usr/bin/env bash
set -euo pipefail

PERM_ALLOW=/etc/fail2ban/allowlist.perm
TMP_ALLOW=/etc/fail2ban/allowlist.tmp
DATE_FMT="%s"

require_root() {
  [[ $(id -u) -eq 0 ]] || { echo "Must be root." >&2; exit 1; }
}

reload_fail2ban() {
  fail2ban-client reload &>/dev/null
}

show_list() {
  local file=$1 title=$2
  echo "=== $title ==="
  if [[ -s "$file" ]]; then
    [[ "$file" == "$TMP_ALLOW" ]] \
      && awk '{ print \$2 }' "$file" \
      || cat "$file"
  else
    echo "(empty)"
  fi
}

add_ip() {
  local file=$1 title=$2
  read -rp "Enter IP to add to $title: " ip
  if grep -Fxq "$ip" "$file"; then
    echo "→ $ip already in $title"
  else
    if [[ "$file" == "$TMP_ALLOW" ]]; then
      echo "\$(date +"$DATE_FMT") \$ip" >> "$file"
    else
      echo "$ip" >> "$file"
    fi
    chmod 644 "$file"
    echo "→ Added $ip to $title"
    reload_fail2ban
  fi
}

remove_ip() {
  local file=$1 title=$2
  read -rp "Enter IP to remove from $title: " ip
  if grep -q "$ip" "$file"; then
    grep -Fxv "$ip" "$file" > "$file.tmp" && mv "$file.tmp" "$file"
    echo "→ Removed $ip from $title"
    reload_fail2ban
  else
    echo "→ $ip not found in $title"
  fi
}

prune_temp() {
  local TTL=$((7*24*3600)) now=$(date +"$DATE_FMT")
  [[ -f "$TMP_ALLOW" ]] || return
  awk -v now="$now" -v ttl="$TTL" '$1 && now - \$1 < ttl {print \$1, \$2}' "$TMP_ALLOW" > "$TMP_ALLOW.tmp"
  mv "$TMP_ALLOW.tmp" "$TMP_ALLOW"
  chmod 644 "$TMP_ALLOW"
}

menu() {
  cat << EOF

Uzenet Fail2Ban List Management
1) Show permanent allowlist
2) Add to permanent allowlist
3) Remove from permanent allowlist
4) Show temporary allowlist
5) Add to temporary allowlist
6) Remove from temporary allowlist
0) Exit
EOF
}

main() {
  require_root
  prune_temp
  while true; do
    menu
    read -rp "Choose an option: " cmd
    case $cmd in
      1) show_list "$PERM_ALLOW" "Permanent allowlist" ;; 
      2) add_ip "$PERM_ALLOW" "Permanent allowlist" ;; 
      3) remove_ip "$PERM_ALLOW" "Permanent allowlist" ;; 
      4) show_list "$TMP_ALLOW" "Temporary allowlist" ;; 
      5) add_ip "$TMP_ALLOW" "Temporary allowlist" ;; 
      6) remove_ip "$TMP_ALLOW" "Temporary allowlist" ;; 
      0) break ;;
      *) echo "Invalid choice." ;;
    esac
done
}

main
