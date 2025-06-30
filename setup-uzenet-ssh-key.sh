#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<EOF
Usage: $0 [-i /path/to/identity_file] [-r user@host] [-p port]

This will:

  1. Generate an Ed25519 keypair (if it doesn't already exist).
  2. Copy the public key to the remote's ~/.ssh/authorized_keys via sshpass.

Options:
  -i   private key path (default: ~/.ssh/uzenet_id_ed25519)
  -r   remote SSH user@host (e.g. deploy@1.2.3.4)
  -p   remote SSH port (default: 22)

On success you'll be able to SSH to the remote with that key, no password required.
EOF
  exit 1
}

# Defaults
KEY="${HOME}/.ssh/uzenet_id_ed25519"
REMOTE=""
PORT=22

while getopts "i:r:p:h" opt; do
  case $opt in
    i) KEY="$OPTARG" ;;
    r) REMOTE="$OPTARG" ;;
    p) PORT="$OPTARG" ;;
    h|*) usage ;;
  esac
done

if [[ -z "$REMOTE" ]]; then
  read -rp "Remote SSH user@host (e.g. deploy@1.2.3.4): " REMOTE
fi

echo
read -rsp "SSH password for ${REMOTE}: " SSHPASS
echo

# 1) Generate keypair if missing
if [[ ! -f "${KEY}" ]]; then
  echo "🔑 Generating new Ed25519 keypair at ${KEY}"
  ssh-keygen -t ed25519 -f "${KEY}" -N "" -C "uzenet deploy key"
else
  echo "🔑 Using existing key at ${KEY}"
fi

PUB="${KEY}.pub"
if [[ ! -f "${PUB}" ]]; then
  echo "❌ Public key not found at ${PUB}"
  exit 1
fi

# 2) Install public key on remote
echo "📋 Installing public key to ${REMOTE}…"
sshpass -p "${SSHPASS}" \
  ssh -o StrictHostKeyChecking=no -p "${PORT}" "${REMOTE}" bash <<EOF
mkdir -p ~/.ssh
chmod 700 ~/.ssh
cat >> ~/.ssh/authorized_keys <<KEY_EOF
$(< "${PUB}")
KEY_EOF
chmod 600 ~/.ssh/authorized_keys
EOF

echo
echo "✅ Public key installed on remote."

# 3) Test
echo "🔧 Testing passwordless SSH…"
if ssh -i "${KEY}" -p "${PORT}" -o BatchMode=yes -o ConnectTimeout=5 "${REMOTE}" "echo OK" 2>/dev/null | grep -q OK; then
  echo "🎉 Passwordless SSH successful!"
  echo "   You can now omit passwords in deploy/status scripts."
  echo "   Use: ssh -i ${KEY} -p ${PORT} ${REMOTE}"
else
  echo "❌ Passwordless SSH failed. Please check ~/.ssh/authorized_keys on the remote."
  exit 1
fi
