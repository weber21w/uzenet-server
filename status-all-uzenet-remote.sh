#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<EOF
Usage: $0 [-r user@host] [-p port] [-P password]

  -r   remote SSH user@host   (e.g. deploy@1.2.3.4)
  -p   remote SSH port        (default: 22)
  -P   SSH password           (insecure; prefer SSHPASS env var or key auth)
  
If neither -P nor \$SSHPASS is set, you will be prompted for the password.
EOF
  exit 1
}

REMOTE="" PORT=22 SSHPASS="${SSHPASS-}"

while getopts "r:p:P:h" opt; do
  case $opt in
    r) REMOTE="$OPTARG" ;;
    p) PORT="$OPTARG"  ;;
    P)
      SSHPASS="$OPTARG"
      echo "⚠ Warning: supplying a password on the command line can be visible in process listings."
      ;;
    h) usage ;;
    *) usage ;;
  esac
done

# Prompt for anything still missing
if [[ -z "$REMOTE" ]]; then
  read -rp "Remote SSH user@host (e.g. deploy@1.2.3.4): " REMOTE
fi

if [[ -z "$SSHPASS" ]]; then
  read -rsp "SSH password: " SSHPASS
  echo
fi

# Ensure sshpass
if ! command -v sshpass &>/dev/null; then
  echo "⚠ Please install sshpass (e.g. apt install sshpass)."
  exit 1
fi

echo
for D in uzenet-*/; do
  [[ -d "$D" ]] || continue
  SERVICE=${D%/}
  echo "=== ${SERVICE}.service on ${REMOTE}:${PORT} ==="
  sshpass -p "$SSHPASS" \
    ssh -o StrictHostKeyChecking=no -p "$PORT" "$REMOTE" \
      "systemctl status ${SERVICE}.service --no-pager"
  echo
done
