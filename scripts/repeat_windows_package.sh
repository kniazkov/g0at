#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
set -euo pipefail
source scripts/windows_shell_pid.sh
root_pid=$(windows_shell_pid)
output=build/ci/package-repetitions
mkdir -p "$output"
rm -f "$output/stop"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/observe_windows_build.ps1 \
    -RootProcessId "$root_pid" -OutputDirectory "$(cygpath -w "$PWD/$output")" \
    > "$output/observer-console.txt" 2>&1 &
observer=$!
finish() {
    result=$?
    trap - EXIT
    touch "$output/stop"
    if ! wait "$observer"; then
        printf 'Observer failed; see observer-console.txt\n' >&2
    fi
    exit "$result"
}
trap finish EXIT
cmake -DBUILD_DIR=build/release -DOUTPUT_DIR="$output" -P scripts/repeat_windows_package.cmake
