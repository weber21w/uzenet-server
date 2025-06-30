#!/bin/bash
set -e

CSV="/var/lib/uzenet/users.csv"
BASE="/var/lib/uzenet/sidecars"
SERVICE_USER="uzenet"
SERVICE_GROUP="uzenet"

mkdir -p "$BASE"
chown "$SERVICE_USER:$SERVICE_GROUP" "$BASE"
chmod 700 "$BASE"

cut -d',' -f4 "$CSV" | tr '|' '\n' | grep -v '^$' | sort -u | while read -r shortname; do
	DIR="$BASE/$shortname"
	if [[ ! -d "$DIR" ]]; then
		echo "→ Creating sidecar dir: $DIR"
		mkdir -p "$DIR"
	fi
	chown "$SERVICE_USER:$SERVICE_GROUP" "$DIR"
	chmod 750 "$DIR"
done
