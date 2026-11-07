#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
# CI-only observer: keep the original build command, pipefail and timeout.
set -euo pipefail
output=build/ci/release-observation
mkdir -p "$output"
rm -f "$output/stop"
{
    date -u
    git rev-parse HEAD
    for name in MSYSTEM MAKEFLAGS MFLAGS CMAKE_BUILD_PARALLEL_LEVEL; do
        printf '%s=%s\n' "$name" "${!name-}"
    done
    for tool in cmake cpack gcc mingw32-make; do
        command -v "$tool" || true
        "$tool" --version || true
    done
    pacman -Q || true
} > "$output/environment.txt" 2>&1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/observe_windows_build.ps1 \
    -OutputDirectory "$(cygpath -w "$PWD/$output")" > "$output/observer-console.txt" 2>&1 &
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
MSYS2_ARG_CONV_EXCL='*' cmd.exe /d /c 'scripts\build_release_mingw.cmd' 2>&1 | tee build/ci/release-build.log
