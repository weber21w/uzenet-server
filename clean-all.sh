#!/bin/bash
echo "[*] Cleaning all subdirectories..."

for dir in */ ; do
	if [ -f "$dir/Makefile" ]; then
		echo "→ Cleaning $dir"
		(make -C "$dir" clean)
	fi
done

echo "[✓] All clean completed."
