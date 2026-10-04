#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
set -Eeuo pipefail
interpreter="$(realpath "${1:?goat executable required}")"
functional="$(realpath "${2:?functional executable required}")"
mkdir -p "${3:?output directory required}"
output="$(realpath "$3")"
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# All existing programs and diagnostics must survive compile/run separation.
(cd "$repo/test/functional" && "$functional" "$interpreter" list.txt compiled)
passed=0
current=''
printf 'Starting binary program testing...\n'
trap 'printf "[fail] %s\nBinary program testing done; total: %d, passed: %d, failed: 1\n" "$current" "$((passed+1))" "$passed"' ERR
ok() { printf '[ ok ] %s\n' "$current"; passed=$((passed+1)); }
same() { diff -u <(tr -d '\r' < "$1") <(tr -d '\r' < "$2"); }
ext=so
if [[ ${OS:-} == Windows_NT ]]; then ext=dll; fi
mkdir -p "$output/temporary" "$output/moved"
workspace="$output/temporary"
if [[ ${OS:-} == Windows_NT ]]; then workspace="$(cygpath -am "$workspace")"; fi
export TMPDIR="$workspace" TMP="$workspace" TEMP="$workspace"
for name in replacement recursive caught uncaught unmatched effects edges; do
    current="compile $name without program execution"
    cp "$repo/test/functional/native_modes/$name.goat" "$output/$name.goat"
    "$interpreter" --lang en --compile --native required "$output/$name.goat" > "$output/compile.out" 2> "$output/compile.err"
    [[ ! -s "$output/compile.out" && ! -s "$output/compile.err" ]]
    [[ -s "$output/$name.gbin" && -s "$output/$name.$ext" ]]
    ok
    current="relocate and execute $name without source or compiler"
    mv "$output/$name.gbin" "$output/moved/$name.gbin"
    mv "$output/$name.$ext" "$output/moved/$name.$ext"
    rm "$output/$name.goat"
    status=0
    CC=goat-no-such-compiler "$interpreter" --lang en --run --native required --save-native "$output/$name.report" "$output/moved/$name.gbin" > "$output/$name.out" 2> "$output/$name.err" || status=$?
    expected=0
    if [[ $name == uncaught ]]; then expected=1; fi
    [[ $status == "$expected" ]]
    same "$repo/test/functional/native_modes/$name.out" "$output/$name.out"
    if [[ -f "$repo/test/functional/native_modes/$name.err" ]]; then
        same "$repo/test/functional/native_modes/$name.err" "$output/$name.err"
    else
        [[ ! -s "$output/$name.err" ]]
    fi
    grep -qx 'preparation=ready' "$output/$name.report"
    grep -Eq '^succeeded=[1-9][0-9]*' "$output/$name.report"
    if [[ $name == recursive ]]; then grep -qx 'retries=1' "$output/$name.report"; fi
    ok
done
current='compiler-free bytecode-only compilation and blocking input at runtime'
printf 'print(input());\n' > "$output/input.goat"
CC=goat-no-such-compiler "$interpreter" --compile --optimize none "$output/input.goat" < /dev/null > "$output/input.compile"
[[ ! -s "$output/input.compile" && ! -e "$output/input.$ext" ]]
rm "$output/input.goat"
printf 'hello\n' | CC=goat-no-such-compiler "$interpreter" --run "$output/input.gbin" > "$output/input.out"
[[ $(cat "$output/input.out") == hello ]]
ok
current='missing library: explicit off, automatic fallback, required rejection'
mv "$output/moved/replacement.$ext" "$output/replacement.saved"
for mode in off auto required; do
    status=0
    CC=goat-no-such-compiler "$interpreter" --run --native "$mode" "$output/moved/replacement.gbin" > "$output/missing-$mode.out" 2> "$output/missing-$mode.err" || status=$?
    if [[ $mode == required ]]; then
        [[ $status != 0 && ! -s "$output/missing-$mode.out" && -s "$output/missing-$mode.err" ]]
    else
        [[ $status == 0 ]]
        same "$repo/test/functional/native_modes/replacement.out" "$output/missing-$mode.out"
    fi
done
ok
current='wrong companion library is rejected before execution'
cp "$output/moved/edges.$ext" "$output/moved/replacement.$ext"
if "$interpreter" --run --native required "$output/moved/replacement.gbin" > "$output/wrong.out" 2> "$output/wrong.err"; then false; fi
[[ ! -s "$output/wrong.out" && -s "$output/wrong.err" ]]
mv "$output/replacement.saved" "$output/moved/replacement.$ext"
ok
current='failed recompilation preserves existing artifacts'
cp "$repo/test/functional/native_modes/replacement.goat" "$output/moved/replacement.goat"
cp "$output/moved/replacement.gbin" "$output/before.gbin"
cp "$output/moved/replacement.$ext" "$output/before.library"
if CC=goat-no-such-compiler "$interpreter" --compile --native required "$output/moved/replacement.goat" > "$output/failed.out" 2> "$output/failed.err"; then false; fi
cmp "$output/before.gbin" "$output/moved/replacement.gbin"
cmp "$output/before.library" "$output/moved/replacement.$ext"
[[ ! -s "$output/failed.out" && -s "$output/failed.err" ]]
ok
current='bytecode-only rebuild does not attach a stale library'
CC=goat-no-such-compiler "$interpreter" --compile "$output/moved/replacement.goat"
CC=goat-no-such-compiler "$interpreter" --run --save-native "$output/off.report" "$output/moved/replacement.gbin" > "$output/off.out"
grep -qx 'preparation=empty' "$output/off.report"
grep -qx 'attempts=0' "$output/off.report"
same "$repo/test/functional/native_modes/replacement.out" "$output/off.out"
ok
current='option and output collisions are rejected before writing'
for args in '--compile --run' '--run --optimize all' '--compile --save-c' '--run --print-analysis' '--compile --save-native'; do
    if "$interpreter" $args "$output/moved/replacement.gbin" "$output/moved/replacement.goat" > "$output/invalid.out" 2> "$output/invalid.err"; then false; fi
    [[ ! -s "$output/invalid.out" && -s "$output/invalid.err" ]]
done
cp "$output/moved/replacement.gbin" "$output/protected"
if "$interpreter" --compile --save-analysis "$output/moved/replacement.gbin" "$output/moved/replacement.goat" > "$output/invalid.out" 2> "$output/invalid.err"; then false; fi
cmp "$output/protected" "$output/moved/replacement.gbin"
ok
current='empty inventory and compiler failure follow compilation policy'
printf 'print("empty");\n' > "$output/empty.goat"
CC=goat-no-such-compiler "$interpreter" --compile --native auto "$output/empty.goat" > "$output/empty.compile"
[[ ! -s "$output/empty.compile" && -s "$output/empty.gbin" ]]
if "$interpreter" --compile --native required "$output/empty.goat" > "$output/empty.compile" 2> "$output/empty.err"; then false; fi
CC=goat-no-such-compiler "$interpreter" --compile --native auto "$output/moved/replacement.goat" > "$output/fallback.compile" 2> "$output/fallback.err"
[[ ! -s "$output/fallback.compile" && -s "$output/fallback.err" ]]
CC=goat-no-such-compiler "$interpreter" --run --save-native "$output/fallback.report" "$output/moved/replacement.gbin" > "$output/fallback.out"
grep -qx 'attempts=0' "$output/fallback.report"
same "$repo/test/functional/native_modes/replacement.out" "$output/fallback.out"
ok
current='invalid or truncated binary fails without execution'
printf 'not bytecode' > "$output/bad.gbin"
if "$interpreter" --run "$output/bad.gbin" > "$output/bad.out" 2> "$output/bad.err"; then false; fi
[[ ! -s "$output/bad.out" && -s "$output/bad.err" ]]
ok
current='unwritable output does not execute the program'
printf 'print("MUST NOT RUN");\n' > "$output/blocked.goat"
mkdir -p "$output/blocked.gbin"
if "$interpreter" --compile "$output/blocked.goat" > "$output/blocked.out" 2> "$output/blocked.err"; then false; fi
[[ ! -s "$output/blocked.out" && -s "$output/blocked.err" ]]
ok
printf 'Binary program testing done; total: %d, passed: %d, failed: 0\n' "$passed" "$passed"
