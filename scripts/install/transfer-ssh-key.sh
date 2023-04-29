#! /bin/bash
cd $(dirname $0)
REMOVE_HOST="$1"
REMOTE_USER="$2"
REMOTE_PASS="$3"
REMOTE_PORT="$4"


if [ -z "$REMOTE_PORT" ]; then REMOTE_PORT="22"; fi
#check to see if we already have a key(probably do..), generate a new one if not
RES=$(ls -l ~/.ssh/ida_*.pub 2>&1 | grep "No such")
#if [ -z "$RES" ]
#then
        #this command will generate a new key if an existing isn't present, otherwise it will leav>
        ssh-keygen -t rsa -f ~/.ssh/id_rsa -q -P ""
#fi

RES=$(dpkg -s sshpass)
if [[ ! "$RES" == *'installed'* ]]; then echo "**NEED SUDO TO INSTALL [sshpass]**"; sudo echo ""; sudo apt install -y sshpass; fi

sshpass -P "$REMOTE_PASS" ssh-copy-id $REMOTE_USER@$REMOTE_HOST -p $REMOTE_PORT
