#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
# Isolated compiler diagnostics, never a replacement for the normal CTest gate.
set -euo pipefail
if [ "$#" -ne 5 ]; then
    printf 'usage: %s GOAT RUNNER REPOSITORY OUTPUT OBSERVER\n' "$0" >&2
    exit 2
fi
goat="$1" runner="$2" repo="$3" output="$4" observer="$5"
mkdir -p "$output"
output="$(cd "$output" && pwd)"
repo="$(cd "$repo" && pwd)"
compiler="$(command -v gcc)"
# Only this additional replay uses the wrapper. Normal CTest runs have ended.
printf '#!/usr/bin/env bash\nexec %q -ftime-report -ftime-report-details -fmem-report "$@"\n' \
    "$compiler" > "$output/gcc-report"
chmod +x "$output/gcc-report"
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
    printf 'replay_status=%s observer_status=%s archive_status=%s end_wall=%s\n' \
        "$status" "$observer_status" "$archive_status" "$(date +%s)" >> "$output/status.log"
    exit "$status"
}
trap finish_observer EXIT
printf 'start_wall=%s compiler=%s\n' "$(date +%s)" "$compiler" > "$output/status.log"
printf 'Additional reporting flags only: -ftime-report -ftime-report-details -fmem-report\n' \
    >> "$output/status.log"
printf 'Original helper scenario, driver, assertions, -O2 and 120/300-second limits retained.\n' \
    >> "$output/status.log"
"$observer" "$output/resources.log" &
observer_pid=$!
CC="$output/gcc-report" "$runner" integration.native_saturation.helpers-builtin \
    "$goat" "$repo" "$replay_work" "$repo/test/data" --keep-work \
    > "$output/runner.stdout" 2> "$output/runner.stderr"
