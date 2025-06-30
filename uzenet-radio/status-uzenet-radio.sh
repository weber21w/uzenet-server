#!/usr/bin/env bash
set -euo pipefail
echo "▶ Status of Uzenet Radio service:"
systemctl status uzenet-radio.service
