#!/bin/bash
cd $(dirname $0)

sudo echo ""
sudo journalctl --rotate
sudo journalctl --vacuum-time=1s

sudo journalctl --vacuum-time=1week
