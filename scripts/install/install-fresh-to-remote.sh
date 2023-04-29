#!/bin/bash
cd $(dirname $0)
#cd ~/uns/scripts/install
REMOTE_HOST="$1"
REMOTE_USER="$2"
REMOTE_PASS="$3"
REMOTE_PORT="$4"
REMOTE_SUDO="$5"

if [[ -z "$REMOTE_HOST" ]] || [[ -z "$REMOTE_USER" ]] || [[ -z "$REMOTE_PASS" ]]; then echo "ERROR requires [HOST] [USER] [PASS] [OPTIONAL-PORT] [OPTIONAL-REMOTE-SUDO]"; exit 1; fi
if [[ -z "$REMOTE_PORT" ]]; then REMOTE_PORT='22'; fi
if [[ -z "$REMOTE_SUDO" ]]; then REMOTE_SUDO="$REMOTE_PASS"; fi

RES=$(dpkg -s sshpass)
if [[ ! "$RES" == *'installed'* ]]; then echo "**NEED SUDO TO INSTALL [sshpass]**"; sudo echo ""; sudo apt install -y sshpass; fi

#transfer the install script
sshpass -p "$REMOTE_PASS" scp -P ${REMOTE_PORT} -o StrictHostKeyChecking=no -r 'install-fresh-local.sh' $REMOTE_USER@$REMOTE_HOST:

#execute the install script, which will download and build everything
sshpass -p "$REMOTE_PASS" ssh -t -o StrictHostKeyChecking=no $REMOTE_USER@$REMOTE_HOST "echo \"$REMOTE_SUDO\" | sudo -S echo \"\"; chmod +x ./install-fresh-local.sh; ./install-fresh-local.sh; rm ./install-fresh-local.sh"






