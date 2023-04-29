#!/bin/bash
cd $(dirname $0)

EMAIL_ADDRESS="$1"

if [[ -z "$EMAIL_ADDRESS" ]]; then echo "0"; exit; fi

FIRST_PART=$(echo "$EMAIL_ADDRESS" | awk -F'@' '{ print $1 }')
SECOND_PART=$(echo "$EMAIL_ADDRESS" | awk -F'@' '{ print $2 }')

if [[ -z "$FIRST_PART" ]] || [[ "$FIRST_PART" == *' '* ]] || [[ "$FIRST_PART" == *['!'@#\$%^\&*()]* ]] || [[ "${#FIRST_PART}" -gt "64" ]]; then echo "0"; exit; fi
if [[ -z "$SECOND_PART" ]] || [[ "$SECOND_PART" == *' '* ]] || [[ "${SECOND_PART:0:1}" == '.' ]] || [[ ! "$SECOND_PART" == *'.'* ]] || [[ "$SECOND_PART" == *['!'@#\$%^\&*()_+]* ]] || [[ "${#SECOND_PART}" -gt "256" ]]; then echo "0"; exit; fi

#check out the actual domain
RES=$(dig "$SECOND_PART" | grep "ANSWER: 0")
if [[ -z "$RES" ]]
then
	echo "1"
else
	echo "0"
fi
