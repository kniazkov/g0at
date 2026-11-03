#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
# Isolated compiler diagnostics, never a replacement for the normal CTest gate.
set -euo pipefail
if [ "$#" -ne 4 ]; then
    printf 'usage: %s GOAT REPOSITORY OUTPUT OBSERVER\n' "$0" >&2
    exit 2
fi
goat="$1" repo="$2" output="$3" observer="$4"
mkdir -p "$output"
output="$(cd "$output" && pwd)"
repo="$(cd "$repo" && pwd)"
compiler="$(command -v gcc)"
observer_pid=""
# Native scenarios create private 0700 directories. Keep them inside the
# container and export an archive, not an unreadable bind-mounted directory.
replay_work="$(mktemp -d "${TMPDIR:-/tmp}/goat-compiler-replay.XXXXXX")"
finish_observer() {
    local status=$? observer_status=0 archive_status=0
    trap - EXIT
    if [ -n "$observer_pid" ]; then
        kill -TERM "$observer_pid" 2>/dev/null || true
        wait "$observer_pid" || observer_status=$?
    fi
    tar -czf "$output/scenario.tar.gz" -C "$replay_work" . || archive_status=$?
    printf 'diagnostic_status=%s observer_status=%s archive_status=%s end_wall=%s\n' \
        "$status" "$observer_status" "$archive_status" "$(date +%s)" >> "$output/status.log"
    exit "$status"
}
trap finish_observer EXIT
printf 'start_wall=%s compiler=%s\n' "$(date +%s)" "$compiler" > "$output/status.log"
printf 'Additional reporting flags only: -ftime-report -ftime-report-details -fmem-report\n' \
    >> "$output/status.log"
printf 'DIAGNOSTIC ONLY: compile deadline 300s; original CTest 120s gate unchanged.\n' \
    >> "$output/status.log"
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror -I"$repo" \
    "$repo/test/support/compiler_diagnostic.c" "$(dirname "$goat")/libcommon.a" \
    -pthread -lm -o "$replay_work/diagnostic-command" \
    > "$output/helper-build.stdout" 2> "$output/helper-build.stderr"
cp "$repo/test/data/native-saturation/driver.c.txt" "$replay_work/driver.c"
cp "$repo/test/data/native-saturation/saturation.goat" "$replay_work/saturation.goat"
cmp "$repo/test/data/native-saturation/driver.c.txt" "$replay_work/driver.c"
sha256sum "$replay_work/driver.c" > "$output/driver.sha256"
GOAT_LANGUAGE=en "$replay_work/diagnostic-command" 120000 "$repo" \
    "$replay_work/export.status" "$goat" --save-c "$replay_work/saturation.goat" \
    > "$replay_work/export.stdout" 2> "$replay_work/export.stderr"
"$observer" "$output/resources.log" &
observer_pid=$!
args=("$compiler" -ftime-report -ftime-report-details -fmem-report
    -std=c11 -O2 -Wall -Wextra -Werror -I"$repo" -iquote"$repo/lib/numbers"
    "$replay_work/driver.c" -lm -o "$replay_work/builtin.exe")
printf '%q ' "${args[@]}" > "$output/compiler.command"
printf '\n' >> "$output/compiler.command"
GOAT_LANGUAGE=en "$replay_work/diagnostic-command" 300000 "$repo" "$replay_work/compiler.status" \
    "${args[@]}" > "$replay_work/compiler.stdout" 2> "$replay_work/compiler.stderr"
# This diagnostic never executes the binary or counts as a passing CTest gate.
