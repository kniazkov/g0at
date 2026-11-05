#!/bin/sh
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
# Only for disposable Linux containers. Read-only, no package or host mutation.
set -eu
if [ "$#" != 1 ]; then
    echo "Usage: $0 evidence-file" >&2
    exit 2
fi
mkdir -p "$(dirname "$1")"
{
    echo 'Python-absent container inventory (conventional interpreter/launcher names)'
    uname -a
    # The base image and installed packages are the complete provenance here.
    # Scan the entire container root filesystem, excluding virtual kernel trees.
    find / \( -path /proc -o -path /sys -o -path /dev \) -prune -o \
        \( -type f -o -type l \) \( -iname 'python' -o -iname 'python[0-9]*' \
        -o -iname 'pythonw*' -o -iname 'pypy*' -o -iname 'py.exe' \) \
        -exec sh -c 'for file do [ ! -x "$file" ] || printf "%s\n" "$file"; done' sh {} + > "$1.executables"
    if command -v apk >/dev/null; then
        apk info | sort > "$1.packages"
    elif command -v dpkg-query >/dev/null; then
        dpkg-query -W -f='${Package}\n' | sort > "$1.packages"
    else
        echo 'Unsupported package inventory' >&2
        exit 1
    fi
    cat "$1.packages"
    if [ -s "$1.executables" ] || grep -Ei '(^|[-_])(libpython|python|pypy)([0-9.-]|$)' "$1.packages"; then
        cat "$1.executables"
        echo 'Python-free container check failed' >&2
        exit 1
    fi
    echo 'No Python interpreter/launcher executable or Python package found.'
} > "$1" 2>&1
cat "$1"
