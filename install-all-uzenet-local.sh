#!/usr/bin/env bash
set -euo pipefail

echo "🚀 Installing all uzenet services locally…"

for D in */; do
  INST=$(find "$D" -maxdepth 1 -type f -name 'install-*.sh' | head -n1)
  if [[ -n "$INST" ]]; then
    SERVICE=${D%/}
    echo
    echo "▶ Installing '$SERVICE' locally…"
    (
      cd "$D"
      sudo bash install-*.sh
    )
    echo "✅ '$SERVICE' installed locally."
  fi
done

echo
echo "🎉 All local installations complete."
