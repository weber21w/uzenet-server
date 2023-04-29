#!/bin/bash
cd $(dirname $0)

SSH_TARGET="$1"
SSH_USERNAME="$2"
SSH_PASSWORD="$3"
LOCAL_TARGET="$4"
REMOTE_TARGET="$5"
CUSTOM_PORT="$6"

if [[ -z "$SSH_USERNAME" ]]; then echo -n "SSH user: "; read SSH_USERNAME; fi
if [[ -z "$SSH_PASSWORD" ]]; then echo -n "SSH pass: "; read SSH_PASSWORD; fi
if [[ -z "$LOCAL_TARGET" ]]; then echo -n "Local file: "; read LOCAL_TARGET; fi
if [[ -z "$REMOTE_TARGET" ]]; then echo -n "Remote file: "; read REMOTE_TARGET; fi

if [[ -z "$SSH_TARGET" ]] || [[ -z "$SSH_USERNAME" ]] || [[ -z "$SSH_PASSWORD" ]] || [[ -z "$LOCAL_TARGET" ]] || [[ -z "$REMOTE_TARGET" ]]
then
	echo "ERROR requires [SSH Target] [SSH User] [SSH Pass] [Local File] [Remote File] [Optional Custom Port]"
	exit 1
fi

if [[ -z "$CUSTOM_PORT" ]]; then CUSTOM_PORT="22"; fi

RES=$(dpkg -s sshpass)
if [[ ! "$RES" == *'installed'* ]]; then echo "**NEED SUDO TO INSTALL [sshpass]**"; sudo echo ""; sudo apt install -y sshpass; fi

sshpass -p "$SSH_PASSWORD" scp -P ${CUSTOM_PORT} -o StrictHostKeyChecking=no -r $LOCAL_TARGET $SSH_USERNAME@$SSH_TARGET:$REMOTE_TARGET
