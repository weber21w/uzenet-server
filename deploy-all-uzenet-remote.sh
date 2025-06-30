#!/usr/bin/env bash
set -euo pipefail

### Helper to show usage
usage() {
  cat <<EOF
Usage: $0 [-r user@host] [-p port] [-P password] [-i identity_file]

  -r  remote SSH user@host   (e.g. deploy@1.2.3.4)
  -p  SSH port (default: 22)
  -P  SSH password (insecure; only used to bootstrap key auth)
  -i  SSH private key file (preferred)

If -i is given, we will attempt key-based auth. If that fails, we’ll
prompt for a password and auto-run setup-uzenet-ssh-key.sh to re-install
the public key.  Otherwise we use sshpass + password for the deploy.
EOF
  exit 1
}

REMOTE="" PORT=22 SSHPASS="${SSHPASS-}" IDENTITY=""

while getopts "r:p:P:i:h" opt; do
  case $opt in
    r) REMOTE="$OPTARG" ;;
    p) PORT="$OPTARG"  ;;
    P) SSHPASS="$OPTARG" ;;
    i) IDENTITY="$OPTARG" ;;
    h|*) usage ;;
  esac
done

# Ensure we have a remote
if [[ -z "$REMOTE" ]]; then
  read -rp "Remote SSH user@host (e.g. deploy@1.2.3.4): " REMOTE
fi

# If identity file provided, verify it and test key auth
if [[ -n "$IDENTITY" ]]; then
  if [[ ! -f "$IDENTITY" ]]; then
    echo "🔑 Identity file '$IDENTITY' not found!"
    exit 1
  fi

  echo "🔑 Testing key-based SSH to $REMOTE..."
  if ! ssh -i "$IDENTITY" \
            -o BatchMode=yes \
            -o ConnectTimeout=5 \
            -p "$PORT" \
            "$REMOTE" true 2>/dev/null
  then
    echo "❌ Key auth failed. Bootstrapping key installation…"

    # Prompt for a password if not already set
    if [[ -z "${SSHPASS}" ]]; then
      read -rsp "SSH password for ${REMOTE}: " SSHPASS
      echo
    fi

    # Reinstall the key
    ./setup-uzenet-ssh-key.sh \
      -r "$REMOTE" \
      -p "$PORT" \
      -i "$IDENTITY" \
      -P "$SSHPASS"

    echo "🔑 Retesting key-based SSH…"
    ssh -i "$IDENTITY" \
        -o BatchMode=yes \
        -p "$PORT" \
        "$REMOTE" true

    echo "✅ Key auth restored."
  fi

  SSH_OPTS="-i $IDENTITY -p $PORT -o StrictHostKeyChecking=no"
else
  # No identity: fall back to sshpass
  if ! command -v sshpass &>/dev/null; then
    echo "⚠ Please install sshpass for password-based SSH."
    exit 1
  fi
  if [[ -z "${SSHPASS}" ]]; then
    read -rsp "SSH password for ${REMOTE}: " SSHPASS
    echo
  fi
  SSH_OPTS="-o StrictHostKeyChecking=no -p $PORT"
fi

# Create throwaway workdir on remote
TIMESTAMP=$(date +%s)
REMOTE_TMP="/tmp/u_
