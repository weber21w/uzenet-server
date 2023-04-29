#!/bin/bash
cd $(dirname $0)

EMAIL_RECEIVER="$1"
EMAIL_SUBJECT="$2"
EMAIL_BODY="$3"
EMAIL_ATTACHMENT="$4"

EMAIL_DATA=$(cat 'email-data.txt')
EMAIL_USER=$(echo "$EMAIL_DATA" | sed '1!d' | tr -d '\r' | tr -d '\n')
EMAIL_PASS=$(echo "$EMAIL_DATA" | sed '2!d' | tr -d '\r' | tr -d '\n')
EMAIL_SERVER=$(echo "$EMAIL_DATA" | sed '3!d' | tr -d '\r' | tr -d '\n')

RES=$(dpkg -s python3)
if [[ ! "$RES" == *'installed'* ]]; then echo "**NEED SUDO TO INSTALL [python3]**"; sudo echo ""; sudo apt install -y python3; fi

if [[ ! -z "$EMAIL_ATTACHMENT" ]]
then
	python3 email-send.py "$EMAIL_USER" "$EMAIL_PASS" "$EMAIL_SERVER" "$EMAIL_RECEIVER" "$EMAIL_SUBJECT" "$EMAIL_BODY" "$EMAIL_ATTACHMENT"
else
	python3 email-send.py "$EMAIL_USER" "$EMAIL_PASS" "$EMAIL_SERVER" "$EMAIL_RECEIVER" "$EMAIL_SUBJECT" "$EMAIL_BODY"
fi
