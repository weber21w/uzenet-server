#!/usr/bin/env bash
set -euo pipefail
echo "📦 Gathering install scripts, Makefiles, services, and source files..."

# Create a temporary staging area
TMPDIR=$(mktemp -d)

# Copy relevant files to staging
find . -type f \( \
	-name 'install-*.sh' -o \
	-name 'remove-*.sh' -o \
	-name 'status-*.sh' -o \
	-name 'Makefile' -o \
	-name '*.service' -o \
	-name '*.c' -o \
	-name '*.h' \
\) -exec cp --parents {} "$TMPDIR" \;

# Create ZIP archive
cd "$TMPDIR"
zip -r "$OLDPWD/uzenet-audit.zip" . >/dev/null
cd "$OLDPWD"

# Cleanup
rm -rf "$TMPDIR"
echo "✅ Audit bundle written to: $PWD/uzenet-audit.zip"
