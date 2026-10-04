#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
set -Eeuo pipefail
interpreter="$(realpath "${1:?goat executable required}")"
output_dir="${2:?output directory required}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$output_dir"
output_dir="$(realpath "$output_dir")"
compiler="$(command -v "${CC:-gcc}")"
export REAL_CC="$(cygpath -am "$compiler")"
passed=0
current='DLL build and exact ABI export'
printf 'Starting Windows native library testing...\n'
trap 'printf "[fail] %s\nWindows native library testing done; total: %d, passed: %d, failed: 1\n" "$current" "$((passed+1))" "$passed"' ERR
clean() { test -z "$(find "$output_dir" -type d -name '*.goat-*' -print -quit)"; }
ok() { clean; printf '[ ok ] %s\n' "$current"; passed=$((passed+1)); }
compile() { "$compiler" -std=c11 -Wall -Wextra -Werror -pedantic-errors -O2 -I"$repo_root/include" "$@"; }
cp "$repo_root/test/functional/native_abi/program.goat" "$output_dir/program.goat"
CC="$REAL_CC" "$interpreter" --lang en --save-library "$output_dir/program.goat" > "$output_dir/stdout.txt" 2> "$output_dir/stderr.txt"
test ! -s "$output_dir/stdout.txt"
test ! -s "$output_dir/stderr.txt"
test ! -e "$output_dir/program.c"
compile -DGOAT_ABI_NO_MAIN "$repo_root/test/functional/native_abi/driver.c" "$repo_root/test/functional/native_windows/dll_host.c" -lm -o "$output_dir/host.exe"
"$output_dir/host.exe" "$output_dir/program.dll"
ok
current='numeric compatibility on the current Windows target'
cp "$repo_root/test/functional/native_numeric/program.goat" "$output_dir/numeric.goat"
CC="$REAL_CC" "$interpreter" --save-library "$output_dir/numeric.goat"
compile "$repo_root/test/functional/native_numeric/driver.c" "$output_dir/numeric.dll" -lm -o "$output_dir/numeric-host.exe"
"$output_dir/numeric-host.exe"
ok
current='default compiler, repeat publication and source export'
env -u CC "$interpreter" --save-library --save-c --print-c "$output_dir/program.goat" > "$output_dir/printed.c"
cmp "$output_dir/printed.c" "$output_dir/program.c"
"$output_dir/host.exe" "$output_dir/program.dll"
ok
current='literal metacharacters and executable paths with spaces'
compile -I"$repo_root/src" "$repo_root/test/functional/native_windows/compiler_fixture.c" "$repo_root/src/lib/windows_command_line.c" "$repo_root/src/lib/allocate.c" -o "$output_dir/compiler & fixture.exe"
fixture="$(cygpath -am "$output_dir/compiler & fixture.exe")"
strange='name & (literal) %PATH% ! space'
mkdir -p "$output_dir/$strange"
cp "$output_dir/program.goat" "$output_dir/$strange/program.goat"
CC="$fixture" "$interpreter" --save-library "$output_dir/$strange/program.goat"
"$output_dir/host.exe" "$output_dir/$strange/program.dll"
cp "$output_dir/program.goat" "$output_dir/-leading.goat"
(cd "$output_dir" && CC="$fixture" "$interpreter" --save-library ./-leading.goat)
test -s "$output_dir/-leading.dll"
ok
current='compiler failures preserve old DLL and remove temporary files'
cp "$output_dir/program.dll" "$output_dir/original.dll"
expect_failure() {
    if CC="$1" "$interpreter" --lang en --save-library "$output_dir/program.goat" > "$output_dir/stdout.txt" 2> "$output_dir/stderr.txt"; then false; fi
    test ! -s "$output_dir/stdout.txt"
    test -s "$output_dir/stderr.txt"
    cmp "$output_dir/program.dll" "$output_dir/original.dll"
    clean
}
expect_failure "$(cygpath -am "$output_dir/missing.exe")"
grep -q 'cannot start compiler' "$output_dir/stderr.txt"
expect_failure "$REAL_CC -O3"
for mode in exit exception absent empty verbose invalid unresolved fast; do
    export FIXTURE_MODE="$mode"
    expect_failure "$fixture"
    case "$mode" in
        exit) grep -q 'exit 23' "$output_dir/stderr.txt"; grep -q 'compiler stderr' "$output_dir/stderr.txt" ;;
        exception) grep -q 'exit 3221225477' "$output_dir/stderr.txt" ;;
        verbose) grep -q 'truncated at 65536' "$output_dir/stderr.txt"; test "$(wc -c < "$output_dir/stderr.txt")" -lt 68000 ;;
        fast) grep -q 'strict floating-point semantics' "$output_dir/stderr.txt" ;;
        unresolved) grep -q 'missing' "$output_dir/stderr.txt" ;;
    esac
done
unset FIXTURE_MODE
ok
current='successful compiler warning and noninteractive input'
FIXTURE_MODE=warning CC="$fixture" "$interpreter" --save-library "$output_dir/program.goat" > "$output_dir/stdout.txt" 2> "$output_dir/stderr.txt"
test ! -s "$output_dir/stdout.txt"
grep -q 'compiler warning on success' "$output_dir/stderr.txt"
"$output_dir/host.exe" "$output_dir/program.dll"
ok
current='loaded DLL cannot be overwritten and remains usable'
cp "$output_dir/program.dll" "$output_dir/original.dll"
CC="$REAL_CC" "$output_dir/compiler & fixture.exe" --locked "$output_dir/program.dll" "$interpreter" "$output_dir/program.goat" > "$output_dir/stdout.txt" 2> "$output_dir/stderr.txt"
grep -q 'file operation failed' "$output_dir/stderr.txt"
cmp "$output_dir/program.dll" "$output_dir/original.dll"
"$output_dir/host.exe" "$output_dir/program.dll"
ok
current='input protection and publication failure'
cp "$output_dir/program.goat" "$output_dir/source.dll"
if CC="$REAL_CC" "$interpreter" --save-library "$output_dir/source.dll" > "$output_dir/stdout.txt" 2> "$output_dir/stderr.txt"; then false; fi
cmp "$output_dir/source.dll" "$output_dir/program.goat"
cp "$output_dir/program.goat" "$output_dir/blocked.goat"
mkdir -p "$output_dir/blocked.dll"
if CC="$REAL_CC" "$interpreter" --save-library "$output_dir/blocked.goat" > "$output_dir/stdout.txt" 2> "$output_dir/stderr.txt"; then false; fi
test -d "$output_dir/blocked.dll"
ok
current='ordinary interpretation needs no compiler'
printf 'print(42);\n' > "$output_dir/interpreted.goat"
"$interpreter" "$output_dir/interpreted.goat" > "$output_dir/expected.txt"
CC=nonexistent-compiler "$interpreter" "$output_dir/interpreted.goat" > "$output_dir/stdout.txt"
cmp "$output_dir/stdout.txt" "$output_dir/expected.txt"
test -s "$output_dir/stdout.txt"
test ! -e "$output_dir/interpreted.dll"
ok
current='empty module and isolated concurrent builds'
printf 'print("MUST NOT EXECUTE");\n' > "$output_dir/empty.goat"
CC="$REAL_CC" "$interpreter" --save-library "$output_dir/empty.goat" > "$output_dir/stdout.txt"
test ! -s "$output_dir/stdout.txt"
cat > "$output_dir/empty-host.c" <<'C'
#include <goat/native_abi.h>
#include <assert.h>
int main(void) {
    const goat_native_module_v1_t *m=goat_native_query_v1(1);
    assert(m && !m->entry_count && !m->entries);
    return 0;
}
C
compile "$output_dir/empty-host.c" "$output_dir/empty.dll" -o "$output_dir/empty-host.exe"
"$output_dir/empty-host.exe"
cp "$output_dir/program.goat" "$output_dir/concurrent.goat"
CC="$REAL_CC" "$interpreter" --save-library "$output_dir/program.goat" &
first=$!
CC="$REAL_CC" "$interpreter" --save-library "$output_dir/concurrent.goat" &
second=$!
wait "$first"
wait "$second"
"$output_dir/host.exe" "$output_dir/program.dll"
"$output_dir/host.exe" "$output_dir/concurrent.dll"
ok
printf 'Windows native library testing done; total: %d, passed: %d, failed: 0\n' "$passed" "$passed"
