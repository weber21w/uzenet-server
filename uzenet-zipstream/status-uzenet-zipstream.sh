#!/bin/bash

SERVICE_NAME=uzenet_zipstream
LOG_FILE=/var/log/${SERVICE_NAME}.log
BINARY=/usr/local/bin/${SERVICE_NAME}_server
SERVICE_FILE=/etc/systemd/system/${SERVICE_NAME}.service
DEFAULT_PORT=57428

get_service_port() {
	local port
	port=$(grep -Po '(?<=--port=)[0-9]+' "$SERVICE_FILE" 2>/dev/null | head -n1)
	[[ -z "$port" ]] && port="$DEFAULT_PORT"
	echo "$port"
}

PORT=$(get_service_port)

if [[ "$1" == "--json" ]]; then
	clients=$(ss -tnp | grep ESTAB | grep ":$PORT" | awk '{print $5}' | cut -d: -f1 | sort | uniq -c)
	client_json=$(echo "$clients" | while read -r count ip; do
		host=$(getent hosts "$ip" | awk '{print $2}')
		[[ -z "$host" ]] && host=null
		echo "{\"ip\": \"$ip\", \"count\": $count, \"host\": \"$host\" }"
	done | jq -s .)
	jq -n \
		--arg service "$(systemctl is-active $SERVICE_NAME)" \
		--arg enabled "$(systemctl is-enabled $SERVICE_NAME 2>/dev/null)" \
		--arg binary "$( [[ -x $BINARY ]] && echo yes || echo no )" \
		--arg logfile "$( [[ -f $LOG_FILE ]] && echo $LOG_FILE || echo missing )" \
		--arg port "$PORT" \
		--arg guest_writable "$( [[ -w $GUEST_DIR ]] && echo yes || echo no )" \
		--arg fail2ban "$(fail2ban-client status uzenet 2>/dev/null | grep 'Currently banned' || echo 'fail2ban not configured')" \
		--argjson clients "$client_json" \
	'{"service":$service,"enabled":$enabled,"binary":$binary,"logfile":$logfile,"port":$port,"guest_dir_writable":$guest_writable,"fail2ban_status":$fail2ban,"clients":$clients}'
	exit 0
fi

# Normal human-readable mode
echo "🟢 Uzenet Zipstream Server Status"
echo "========================================"

echo -n "Service: "; systemctl is-active --quiet "$SERVICE_NAME" && echo "RUNNING" || echo "STOPPED"
echo -n "Enabled at boot: "; systemctl is-enabled "$SERVICE_NAME" 2>/dev/null || echo "No"
echo -n "Binary present: "; [[ -x "$BINARY" ]] && echo "Yes" || echo "No"
echo -n "Service file: "; [[ -f "$SERVICE_FILE" ]] && echo "Exists" || echo "Missing"
echo -n "Log file: "; [[ -f "$LOG_FILE" ]] && echo "$LOG_FILE" || echo "Missing"
echo -n "Listening port: "; echo "$PORT"
echo -n "Guest dir writable: "; [[ -w "$GUEST_DIR" ]] && echo "Yes" || echo "No"

echo
echo "🔌 Listening TCP Sockets:"
echo "------------------------"
ss -tulnp | grep ":$PORT" || echo "(not listening on port $PORT)"

echo
echo "🌐 Connected Clients (by IP):"
echo "-----------------------------"
clients=$(ss -tnp | grep ESTAB | grep ":$PORT" | awk '{print $5}' | cut -d: -f1 | sort)
if [[ -z "$clients" ]]; then
	echo "(no active connections)"
else
	echo "$clients" | uniq -c | while read -r count ip; do
		host=$(getent hosts "$ip" | awk '{print $2}')
		[[ -z "$host" ]] && host="(no DNS)"
		printf "%-16s (%s connections) [%s]\n" "$ip" "$count" "$host"
	done
fi

echo
echo "📄 Last 10 Log Entries:"
echo "------------------------"
[[ -f "$LOG_FILE" ]] && tail -n 10 "$LOG_FILE" || echo "(no log file found)"

echo
echo "🗾 Systemd Summary:"
echo "------------------------"
systemctl status "$SERVICE_NAME" --no-pager --lines=5 || echo "(service not found)"

echo
echo "🛡️ Fail2Ban Status:"
echo "------------------------"
fail2ban-client status uzenet 2>/dev/null || echo "(fail2ban not configured or not monitoring this service)"
