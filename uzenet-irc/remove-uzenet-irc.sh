#!/bin/bash
set -e

echo "Stopping uzenet-irc.service..."
systemctl disable --now uzenet-irc.service 2>/dev/null || true

echo "Removing systemd service..."
rm -f /etc/systemd/system/uzenet-irc.service
systemctl daemon-reexec
systemctl daemon-reload

echo "Removing rsyslog config..."
rm -f /etc/rsyslog.d/uzenet-irc.conf
systemctl restart rsyslog

echo "Removing logrotate config..."
rm -f /etc/logrotate.d/uzenet-irc.conf

echo "Removing binary..."
rm -f /usr/local/bin/uzenet-irc-server

echo "Removing service user and home..."
userdel -r uzenet 2>/dev/null || true

echo "Cleaning build artifacts..."
make clean || true

echo "Done. uzenet-irc has been removed."
