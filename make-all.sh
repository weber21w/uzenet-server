#!/bin/bash

if [[ "$1" == "--install-deps" ]]; then
	./install-dependencies.sh || exit 1
fi

echo "[*] Running make in all subdirectories..."

for dir in */ ; do
	if [ -f "$dir/Makefile" ]; then
		echo "→ Entering $dir"
		(make -C "$dir" all) || { echo "[!] Build failed in $dir"; exit 1; }
	fi
done

echo "[✓] All builds completed."
