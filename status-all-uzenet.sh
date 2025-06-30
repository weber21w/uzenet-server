#!/bin/bash

UFS_SERVICES=(
	uzenet_fatfs
	uzenet_updater
	uzenet_patchsync
)

print_section() {
	local name="$1"
	echo
	echo "✨ $name"
	echo "$(printf -- "%.0s─" {1..40})"
}

for SERVICE in "${UFS_SERVICES[@]}"; do
	print_section "Service: $SERVICE"

	if ! systemctl list-units --type=service | grep -q "$SERVICE"; then
		echo "Service not found."
		continue
	fi

	echo -n "Status:       "; systemctl is-active --quiet "$SERVICE" && echo "RUNNING" || echo "STOPPED"
	echo -n "Enabled:      "; systemctl is-enabled "$SERVICE" 2>/dev/null || echo "No"
	echo -n "Binary:       "; which "${SERVICE}_server" 2>/dev/null || echo "(not found in PATH)"

	SERVICE_LOG="/var/log/${SERVICE}.log"
	echo -n "Log file:     "; [[ -f "$SERVICE_LOG" ]] && echo "$SERVICE_LOG" || echo "Missing"

	echo "Recent Logs:"
	[[ -f "$SERVICE_LOG" ]] && tail -n 5 "$SERVICE_LOG" || echo "(no log entries found)"

	PORT=$(grep -Po '(?<=--port=)[0-9]+' /etc/systemd/system/${SERVICE}.service 2>/dev/null | head -n1)
	[[ -z "$PORT" ]] && PORT="(unknown)"
	echo "Port:         $PORT"
	done

print_section "Fail2Ban Status"
fail2ban-client status uzenet 2>/dev/null || echo "(fail2ban not monitoring 'uzenet')"

print_section "Active Connections Summary"
ss -tnp | grep ESTAB | grep -E ":(57428|57429|57430)" | awk '{print $5}' | cut -d: -f1 | sort | uniq -c
