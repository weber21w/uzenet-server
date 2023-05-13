#!/bin/bash
cd $(dirname $0)

BLOCK_IP="$1"

sudo ufw deny from $1 to any
