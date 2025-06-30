#!/bin/bash
set -e

echo "→ Installing uzenet-identity service..."

# 1. Install binary
install -m 755 uzenet-identity-server /usr/local/bin/

# 2. Install systemd service
cat > /etc/systemd/system/uzenet-identity.service <<EOF
[Unit]
Description=Uzenet Identity Server
After=network.target

[Service]
ExecStart=/usr/local/bin/uzenet-identity-server
Restart=on-failure
User=uzenet
Group=uzenet

[Install]
WantedBy=multi-user.target
EOF

# 3. Enable & start
systemctl daemon-reexec
systemctl daemon-reload
systemctl enable uzenet-identity
systemctl restart uzenet-identity
echo "→ uzenet-identity service installed and running."

# 4. Ensure user DB exists
CSV="/var/lib/uzenet/users.csv"
if [ ! -f "$CSV" ]; then
	echo "→ Creating blank user database at $CSV"
	mkdir -p /var/lib/uzenet
	touch "$CSV"
	chown uzenet:uzenet "$CSV"
	chmod 600 "$CSV"
fi

# 5. Developer environment base dir
BASE="/var/lib/uzenet/sidecars"
mkdir -p "$BASE"
chown uzenet:uzenet "$BASE"
chmod 700 "$BASE"

# 6. Parse users.csv for dev access
echo "→ Checking developer access shortnames..."
cut -d',' -f4 "$CSV" | tr '|' '\n' | grep -v '^$' | sort -u | while read -r shortname; do
	DIR="$BASE/$shortname"
	if [[ ! -d "$DIR" ]]; then
		echo "→ Creating $DIR"
		mkdir -p "$DIR"
	fi
	chown uzenet:uzenet "$DIR"
	chmod 750 "$DIR"
done

# 7. Install daily cron job (only once)
echo "→ Installing cron job for dev env check..."
LINE="@daily /usr/local/bin/uzenet-identity-devcheck.sh"
CRONTAB_TMP=$(mktemp)
crontab -l 2>/dev/null > "$CRONTAB_TMP" || true
if grep -Fxq "$LINE" "$CRONTAB_TMP"; then
	echo "→ Cron already installed."
else
	echo "$LINE" >> "$CRONTAB_TMP"
	crontab "$CRONTAB_TMP"
	echo "→ Cron job added."
fi
rm -f "$CRONTAB_TMP"

# 8. Write check script to /usr/local/bin
cat > /usr/local/bin/uzenet-identity-devcheck.sh <<'EOS'
#!/bin/bash
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
EOS

chmod +x /usr/local/bin/uzenet-identity-devcheck.sh

echo "✅ All done."
