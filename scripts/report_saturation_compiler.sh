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
original_compile_status=0 split_compile_status=not-run split_run_status=not-run
printf 'phase=original_compile start_wall=%s\n' "$(date +%s)" >> "$output/status.log"
GOAT_LANGUAGE=en "$replay_work/diagnostic-command" 300000 "$repo" "$replay_work/compiler.status" \
    "${args[@]}" > "$replay_work/compiler.stdout" 2> "$replay_work/compiler.stderr" \
    || original_compile_status=$?
printf 'phase=original_compile end_wall=%s status=%s\n' \
    "$(date +%s)" "$original_compile_status" >> "$output/status.log"
# A failed compile may be compared only after confirmed owned-tree cleanup.
original_result="$(cat "$replay_work/compiler.status" 2>/dev/null || true)"
if [[ ! "$original_result" =~ ^status=[0-9]+\ exit=-?[0-9]+\ signal=[0-9]+\ system=0\ cleanup=0\ deadline_ms=300000$ ]]; then
    printf 'comparison_skipped=unsafe_cleanup_or_missing_status original_compile_status=%s partition_status=skipped split_compile_status=skipped split_run_status=skipped\n' \
        "$original_compile_status" >> "$output/status.log"
    [ "$original_compile_status" -ne 0 ] || original_compile_status=125
    exit "$original_compile_status"
fi
# Attempt the independent comparison after a safe failure or cleaned-up timeout.
partition_status=0
printf 'phase=partition start_wall=%s\n' "$(date +%s)" >> "$output/status.log"
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
    "$repo/test/support/partition_saturation_diagnostic.c" \
    -o "$replay_work/partition-driver" \
    > "$replay_work/partition-build.stdout" 2> "$replay_work/partition-build.stderr" \
    || partition_status=$?
if [ "$partition_status" -eq 0 ]; then
    "$replay_work/diagnostic-command" 120000 "$repo" "$replay_work/partition.status" \
        "$replay_work/partition-driver" "$replay_work/driver.c" "$replay_work" \
        > "$replay_work/partition.stdout" 2> "$replay_work/partition.stderr" \
        || partition_status=$?
fi
if [ "$partition_status" -eq 0 ]; then
    cmp "$replay_work/driver.c" "$replay_work/reconstructed-driver.c" || partition_status=$?
fi
if [ "$partition_status" -eq 0 ]; then
    sha256sum "$replay_work/driver.c" "$replay_work/reconstructed-driver.c" \
        > "$output/partition-proof.sha256" || partition_status=$?
fi
printf 'phase=partition end_wall=%s status=%s\n' \
    "$(date +%s)" "$partition_status" >> "$output/status.log"
if [ "$partition_status" -eq 0 ]; then
    split_args=("$compiler" -ftime-report -ftime-report-details -fmem-report
        -std=c11 -O2 -Wall -Wextra -Werror -I"$repo" -iquote"$repo/lib/numbers"
        "$replay_work/main.c" "$replay_work"/part*.c -lm -o "$replay_work/split64.exe")
    printf '%q ' "${split_args[@]}" > "$output/split64.command"
    printf '\n' >> "$output/split64.command"
    printf 'phase=split_compile start_wall=%s\n' "$(date +%s)" >> "$output/status.log"
    split_compile_status=0
    GOAT_LANGUAGE=en "$replay_work/diagnostic-command" 120000 "$repo" \
        "$replay_work/split64-compile.status" "${split_args[@]}" \
        > "$replay_work/split64-compile.stdout" 2> "$replay_work/split64-compile.stderr" \
        || split_compile_status=$?
    printf 'phase=split_compile end_wall=%s status=%s\n' \
        "$(date +%s)" "$split_compile_status" >> "$output/status.log"
    if [ "$split_compile_status" -eq 0 ]; then
        split_run_status=0
        printf 'phase=split_run start_wall=%s\n' "$(date +%s)" >> "$output/status.log"
        "$replay_work/diagnostic-command" 120000 "$repo" "$replay_work/split64-run.status" \
            "$replay_work/split64.exe" \
            > "$replay_work/split64-run.stdout" 2> "$replay_work/split64-run.stderr" \
            || split_run_status=$?
        printf 'phase=split_run end_wall=%s status=%s\n' \
            "$(date +%s)" "$split_run_status" >> "$output/status.log"
    fi
fi
printf 'original_compile_status=%s partition_status=%s split_compile_status=%s split_run_status=%s\n' \
    "$original_compile_status" "$partition_status" "$split_compile_status" "$split_run_status" \
    >> "$output/status.log"
# Neither diagnostic can replace the normal CTest gate. Preserve both failures.
[ "$original_compile_status" -eq 0 ] || exit "$original_compile_status"
[ "$partition_status" -eq 0 ] || exit "$partition_status"
[ "$split_compile_status" = 0 ] || exit "$split_compile_status"
[ "$split_run_status" = 0 ] || exit "$split_run_status"
