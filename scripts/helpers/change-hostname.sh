#!/bin/bash
cd $(dirname $0)

SUDO_PASS=$(cat pw.txt)
echo "$SUDO_PASS" | sudo -S echo ""
sleep 1

NEW_HOSTNAME="$1"

sudo hostnamectl set-hostname $NEW_HOSTNAME
sudo sed -i "/127.0.1.1/c\127.0.1.1       $NEW_HOSTNAME" /etc/hosts

