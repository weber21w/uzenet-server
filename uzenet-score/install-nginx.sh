#!/usr/bin/env bash
set -e

if [ "$#" -lt 3 ]; then
  cat <<EOF
Usage: sudo $0 domain1 [domain2 ...] email

Example:
  sudo $0 example.com www.example.com you@example.org
EOF
  exit 1
fi

# Extract email (last arg) and domains (all but last)
EMAIL="${@: -1}"
DOMAINS=("${@:1:$(($#-1))}")

# Join domains with spaces and commas
DARGS=""
CARGS=""
for d in "${DOMAINS[@]}"; do
  DARGS+=" -d $d"
  CARGS+="$d,"
done
# remove trailing comma
CARGS="${CARGS%,}"

echo "→ Updating apt, installing Nginx and Certbot..."
apt update
DEBIAN_FRONTEND=noninteractive apt install -y nginx certbot python3-certbot-nginx

echo "→ Creating web root and sample index..."
mkdir -p /var/www/uzenet
cat > /var/www/uzenet/index.html <<HTML
<!doctype html>
<title>UZENET Lobby</title>
<h1>Welcome to UZENET</h1>
<p>Your lobby is up—real-time stats, leaderboards, screenshots, all here.</p>
HTML
chown -R www-data:www-data /var/www/uzenet
chmod -R 755 /var/www/uzenet

echo "→ Writing Nginx site config for ${DOMAINS[*]}..."
NGINX_CONF=/etc/nginx/sites-available/uzenet.conf
cat > "$NGINX_CONF" <<NGINX
server {
    listen 80;
    server_name $CARGS;

    root /var/www/uzenet;
    index index.html;

    # Proxy WebSocket/API endpoints to your game-room daemon:
    location /api/   { proxy_pass http://127.0.0.1:8080; }
    location /ws/    { proxy_pass http://127.0.0.1:8080; 
                       proxy_http_version 1.1;
                       proxy_set_header Upgrade \$http_upgrade;
                       proxy_set_header Connection "upgrade";
    }
}
NGINX

ln -sf "$NGINX_CONF" /etc/nginx/sites-enabled/uzenet.conf
rm -f /etc/nginx/sites-enabled/default

echo "→ Testing Nginx config..."
nginx -t

echo "→ Starting Nginx..."
systemctl enable nginx
systemctl restart nginx

echo "→ Obtaining TLS certificates via Certbot..."
certbot --nginx $DARGS \
  --email "$EMAIL" \
  --agree-tos \
  --no-eff-email \
  --noninteractive

echo "→ Enabling and starting certbot.timer..."
systemctl enable certbot.timer
systemctl start  certbot.timer

echo "→ Performing dry-run renewal test..."
certbot renew --dry-run

echo -e "\nAll done! Your site is live at https://${DOMAINS[0]}/ with auto-renewing certificates."
