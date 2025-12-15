#!/usr/bin/env bash
set -euo pipefail
echo "▶ Status of Uzenet SSH service:"
systemctl status uzenet-ssh.service
