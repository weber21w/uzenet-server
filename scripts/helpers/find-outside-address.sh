#!/bin/bash
cd $(dirname $0)

#RES=$(ip addr show eth0 | grep inet | awk '{ print $2; }' | sed 's/\/.*$//')
RES=$(curl --silent http://icanhazip.com)

echo "$RES"

