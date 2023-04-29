#!/bin/bash
cd $(dirname $0)

RES=$(dpkg -s python3)
if [[ ! "$RES" == *'installed'* ]]; then echo "**NEED SUDO TO INSTALL [python3]**"; sudo echo ""; sudo apt install -y python3; fi

EMAIL_DATA=$(cat 'email-data.txt')
EMAIL_USER=$(echo "$EMAIL_DATA" | sed '1!d' | tr -d '\r' | tr -d '\n')
EMAIL_PASS=$(echo "$EMAIL_DATA" | sed '2!d' | tr -d '\r' | tr -d '\n')
EMAIL_SERVER=$(echo "$EMAIL_DATA" | sed '3!d' | tr -d '\r' | tr -d '\n')

#rm -r ./attachments/* #get rid of any attachments from the previous check
python3 get-oldest-unread-email-new.py "$EMAIL_USER" "$EMAIL_PASS" "$EMAIL_SERVER"
