#!/bin/bash
set -e

echo "Installing uzenet-video service..."

# Step 1: Ensure dependencies
echo "Installing build dependencies..."
sudo apt update
sudo apt install -y \
	build-essential \
	ffmpeg \
	libavcodec-dev \
	libavformat-dev \
	libavutil-dev \
	libswscale-dev \
	libswresample-dev \
	libpthread-stubs0-dev


echo "[*] Downloading standalone yt-dlp binary..."
cd "$(dirname "$0")"
curl -L https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_linux -o yt-dlp
chmod +x yt-dlp

# Step 2: Build the binary
make

# Step 3: Install binary
sudo install -m 755 uzenet-video /usr/local/bin/

# Step 4: Create systemd service
echo "Creating systemd service..."
sudo tee /etc/systemd/system/uzenet-video.service > /dev/null <<EOF
[Unit]
Description=Uzenet Video Stream Proxy
After=network.target

[Service]
ExecStart=/usr/local/bin/uzenet-video
Restart=always
User=uzebox
WorkingDirectory=/usr/local/bin

[Install]
WantedBy=multi-user.target
EOF

# Step 5: Enable and start the service
echo "Enabling and starting service..."
sudo systemctl daemon-reexec
sudo systemctl daemon-reload
sudo systemctl enable --now uzenet-video.service

echo "✅ uzenet-video installed and running."
