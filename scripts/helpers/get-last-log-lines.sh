#!/bin/bash
cd $(dirname $0)

NUM_LINES="$1"
if [[ -z "$NUM_LINES" ]]; then NUM_LINES="256"; fi

sudo journalctl -u uzenet-server.service -b | tail -n${NUM_LINES}
