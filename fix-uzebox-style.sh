#!/bin/bash
fix_file() {
	unexpand --first-only --tabs=4 -a "$1" > "$1.tmp" || return 1

	sed -i \
		-e 's/\bif[ \t]*(/if(/g' \
		-e 's/\bwhile[ \t]*(/while(/g' \
		-e 's/\bfor[ \t]*(/for(/g' \
		-e 's/)[ \t]*{/){/g' \
		-e 's/}[ \t]*else[ \t]*if[ \t]*(/}else if(/g' \
		-e 's/}[ \t]*else[ \t]*{/}else{/g' \
		\
		-e 's/\([a-zA-Z0-9_ *]\+\)[ \t]*(\([^\)]\)/\1(\2/g' \
		-e 's/(\([ \t]*void[ \t]*\))$/\(void\)/g' \
		"$1.tmp"

	mv "$1.tmp" "$1"
}

export -f fix_file

find . -name '*.[ch]' -exec bash -c 'fix_file "$0"' {} \;
