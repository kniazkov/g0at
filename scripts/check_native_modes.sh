#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
set -Eeuo pipefail
interpreter="$(realpath "${1:?goat executable required}")"
functional="$(realpath "${2:?functional test executable required}")"
mkdir -p "${3:?output directory required}"
output_dir="$(realpath "$3")"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
fixtures="$repo_root/test/functional/native_modes"
compiler="$(command -v "${CC:-gcc}")"
compiler_env="$compiler"
ext=so
exe=
shared=(-shared -fPIC)
if [[ ${OS:-} == Windows_NT ]]; then
    ext=dll
    exe=.exe
    shared=(-shared -Wl,--exclude-all-symbols)
    compiler_env="$(cygpath -am "$compiler")"
fi
export CC="$compiler_env"
export GOAT_LANGUAGE=en
mkdir -p "$output_dir/temporary"
workspace_env="$output_dir/temporary"
if [[ ${OS:-} == Windows_NT ]]; then workspace_env="$(cygpath -am "$workspace_env")"; fi
export TMPDIR="$workspace_env" TMP="$workspace_env" TEMP="$workspace_env"
# All existing expectations also apply to mixed native/bytecode execution.
(cd "$repo_root/test/functional" && "$functional" "$interpreter" list.txt auto)
passed=0
current=''
printf 'Starting native modes testing...\n'
trap 'printf "[fail] %s\nNative modes testing done; total: %d, passed: %d, failed: 1\n" "$current" "$((passed+1))" "$passed"' ERR
ok() { printf '[ ok ] %s\n' "$current"; passed=$((passed+1)); }
run() {
    local stem="$1"
    shift
    local status=0
    "$interpreter" --lang en "$@" > "$output_dir/$stem.out" 2> "$output_dir/$stem.err" || status=$?
    printf '%s\n' "$status" > "$output_dir/$stem.status"
    for stream in out err; do
        tr -d '\r' < "$output_dir/$stem.$stream" > "$output_dir/normalized"
        mv "$output_dir/normalized" "$output_dir/$stem.$stream"
    done
}
same_text() { diff -u <(tr -d '\r' < "$1") <(tr -d '\r' < "$2"); }
field() { grep -qx "$2=$3" "$1"; }
positive() { awk -F= -v key="$2" '$1==key && $2 ~ /^[0-9]+$/ && $2>0 {found=1} END {exit !found}' "$1"; }
for name in numeric edges recursive caught uncaught unmatched effects; do
    for mode in off auto required; do
        current="$name (native=$mode)"
        stem="$name-$mode"
        report="$output_dir/$stem.report"
        run "$stem" --native "$mode" --save-native "$report" "$fixtures/$name.goat"
        expected_status=0
        if [[ $name == uncaught ]]; then expected_status=1; fi
        [[ $(cat "$output_dir/$stem.status") == "$expected_status" ]]
        same_text "$fixtures/$name.out" "$output_dir/$stem.out"
        if [[ -f "$fixtures/$name.err" ]]; then
            same_text "$fixtures/$name.err" "$output_dir/$stem.err"
        else
            [[ ! -s "$output_dir/$stem.err" ]]
        fi
        field "$report" mode "$mode"
        if [[ $mode == off ]]; then
            field "$report" preparation disabled
            field "$report" attempts 0
            field "$report" succeeded 0
            field "$report" retries 0
        else
            field "$report" preparation ready
            positive "$report" bound
            positive "$report" succeeded
            if [[ $name == recursive ]]; then field "$report" retries 1; fi
        fi
        if [[ $mode != off ]]; then
            same_text "$output_dir/$name-off.out" "$output_dir/$stem.out"
            same_text "$output_dir/$name-off.err" "$output_dir/$stem.err"
            same_text "$output_dir/$name-off.status" "$output_dir/$stem.status"
        fi
        ok
    done
done
current='default mode never invokes a compiler'
CC=goat-no-such-compiler run default --save-native "$output_dir/default.report" "$fixtures/numeric.goat"
[[ $(cat "$output_dir/default.status") == 0 && ! -s "$output_dir/default.err" ]]
same_text "$fixtures/numeric.out" "$output_dir/default.out"
field "$output_dir/default.report" mode off
field "$output_dir/default.report" attempts 0
ok
current='print report agrees with sidecar and preserves program output'
run printed --native required --print-native --save-native "$output_dir/printed.report" "$fixtures/caught.goat"
[[ $(cat "$output_dir/printed.status") == 0 && ! -s "$output_dir/printed.err" ]]
{
    cat "$fixtures/caught.out"
    printf '\n'
    cat "$output_dir/printed.report"
} > "$output_dir/printed.expected"
same_text "$output_dir/printed.expected" "$output_dir/printed.out"
ok
for mode in auto required; do
    current="empty inventory (native=$mode)"
    CC=goat-no-such-compiler run "empty-$mode" --native "$mode" --save-native "$output_dir/empty-$mode.report" "$fixtures/empty.goat"
    field "$output_dir/empty-$mode.report" preparation empty
    field "$output_dir/empty-$mode.report" attempts 0
    if [[ $mode == auto ]]; then
        [[ $(cat "$output_dir/empty-$mode.status") == 0 && ! -s "$output_dir/empty-$mode.err" ]]
        same_text "$fixtures/empty.out" "$output_dir/empty-$mode.out"
    else
        [[ $(cat "$output_dir/empty-$mode.status") == 1 && ! -s "$output_dir/empty-$mode.out" ]]
        grep -q 'Native preparation failed (empty)' "$output_dir/empty-$mode.err"
    fi
    ok
done
current='build compiler substitutes'
"$compiler" -std=c11 -Wall -Wextra -Werror -pedantic-errors -O2 -I"$repo_root/include" \
    "${shared[@]}" -DGOAT_NATIVE_BUILD "$repo_root/test/functional/native_pipeline/empty.c" -o "$output_dir/empty.$ext"
"$compiler" -std=c11 -Wall -Wextra -Werror -pedantic-errors -O2 \
    "$repo_root/test/functional/native_pipeline/compiler.c" -o "$output_dir/invalid-compiler$exe"
cp "$output_dir/invalid-compiler$exe" "$output_dir/wrong-compiler$exe"
export GOAT_PIPELINE_WRONG_LIBRARY="$output_dir/empty.$ext"
if [[ ${OS:-} == Windows_NT ]]; then
    export GOAT_PIPELINE_WRONG_LIBRARY="$(cygpath -am "$GOAT_PIPELINE_WRONG_LIBRARY")"
fi
for stage in compile load bind; do
    broken=goat-no-such-compiler
    if [[ $stage == load ]]; then broken="$output_dir/invalid-compiler$exe"; fi
    if [[ $stage == bind ]]; then broken="$output_dir/wrong-compiler$exe"; fi
    if [[ ${OS:-} == Windows_NT && $stage != compile ]]; then broken="$(cygpath -am "$broken")"; fi
    for mode in auto required; do
        current="$stage failure (native=$mode)"
        stem="$stage-$mode"
        CC="$broken" run "$stem" --native "$mode" --save-native "$output_dir/$stem.report" "$fixtures/numeric.goat"
        field "$output_dir/$stem.report" preparation "$stage-error"
        field "$output_dir/$stem.report" attempts 0
        grep -q "Native preparation failed ($stage-error)" "$output_dir/$stem.err"
        if [[ $mode == auto ]]; then
            [[ $(cat "$output_dir/$stem.status") == 0 ]]
            same_text "$fixtures/numeric.out" "$output_dir/$stem.out"
        else
            [[ $(cat "$output_dir/$stem.status") == 1 && ! -s "$output_dir/$stem.out" ]]
        fi
        ok
    done
done
reject() {
    current="reject $*"
    run rejected "$@"
    [[ $(cat "$output_dir/rejected.status") == 1 && -s "$output_dir/rejected.err" && ! -s "$output_dir/rejected.out" ]]
    ok
}
reject --native
reject --native '' "$fixtures/empty.goat"
reject --native unknown "$fixtures/empty.goat"
reject --native --print-native "$fixtures/empty.goat"
reject --save-native
reject --save-native '' "$fixtures/empty.goat"
reject --native auto --optimize none "$fixtures/empty.goat"
reject --optimize none --native required "$fixtures/empty.goat"
reject --native required --save-c "$fixtures/empty.goat"
reject --native auto --print-c "$fixtures/empty.goat"
reject --native auto --save-library "$fixtures/empty.goat"
reject --print-native --save-c "$fixtures/empty.goat"
reject --save-native "$output_dir/unwanted.report" --save-library "$fixtures/empty.goat"
current='native report cannot overwrite source or a hard link to it'
cp "$fixtures/empty.goat" "$output_dir/protected.goat"
reject --save-native "$output_dir/protected.goat" "$output_dir/protected.goat"
ln -f "$output_dir/protected.goat" "$output_dir/protected-link.txt"
reject --save-native "$output_dir/protected-link.txt" "$output_dir/protected.goat"
same_text "$fixtures/empty.goat" "$output_dir/protected.goat"
current='report write failure remains an error'
run write-error --save-native "$output_dir/missing/dir/report.txt" "$fixtures/empty.goat"
[[ $(cat "$output_dir/write-error.status") == 1 ]]
grep -q 'Could not write native report' "$output_dir/write-error.err"
ok
current='source-only export works without compiler and does not execute Goat'
cp "$fixtures/caught.goat" "$output_dir/export.goat"
CC=goat-no-such-compiler run export --native off --save-c "$output_dir/export.goat"
[[ $(cat "$output_dir/export.status") == 0 && ! -s "$output_dir/export.out" && ! -s "$output_dir/export.err" && -s "$output_dir/export.c" ]]
ok
current='all native workspaces are removed'
[[ -z $(find "$output_dir/temporary" -mindepth 1 -print -quit) ]]
ok
printf 'Native modes testing done; total: %d, passed: %d, failed: 0\n' "$passed" "$passed"
