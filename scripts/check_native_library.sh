#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
set -Eeuo pipefail
interpreter="$(realpath "${1:?goat executable required}")"
output_dir="${2:?output directory required}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$output_dir"
output_dir="$(realpath "$output_dir")"
compiler="$(command -v "${CC:-cc}")"
passed=0
current='compile shared library and call numeric adapters'
printf 'Starting native library testing...\n'
trap 'printf "[fail] %s\nNative library testing done; total: %d, passed: %d, failed: 1\n" "$current" "$((passed+1))" "$passed"' ERR
clean() { test -z "$(find "$output_dir" -type d -name '*.goat-??????' -print -quit)"; }
ok() { clean; printf '[ ok ] %s\n' "$current"; passed=$((passed+1)); }
compile() { "$compiler" -std=c11 -Wall -Wextra -Werror -pedantic-errors -O2 -I"$repo_root/include" "$@"; }
cp "$repo_root/test/functional/native_abi/program.goat" "$output_dir/program.goat"
CC="$compiler" "$interpreter" --lang en --save-library "$output_dir/program.goat" > "$output_dir/stdout.txt" 2> "$output_dir/stderr.txt"
test ! -s "$output_dir/stdout.txt"
test ! -s "$output_dir/stderr.txt"
test ! -e "$output_dir/program.c"
compile "$repo_root/test/functional/native_abi/driver.c" "$output_dir/program.so" -lm -o "$output_dir/host"
"$output_dir/host"
ok
current='ordinary interpretation needs no compiler'
printf 'print(42);\n' > "$output_dir/interpreted.goat"
"$interpreter" "$output_dir/interpreted.goat" > "$output_dir/expected.txt"
CC="$output_dir/missing-compiler" "$interpreter" "$output_dir/interpreted.goat" > "$output_dir/stdout.txt"
cmp "$output_dir/stdout.txt" "$output_dir/expected.txt"
test -s "$output_dir/stdout.txt"
test ! -e "$output_dir/interpreted.so"
ok
current='combined source and library output, default compiler'
env -u CC "$interpreter" --save-library --save-c --print-c "$output_dir/program.goat" > "$output_dir/printed.c"
cmp "$output_dir/printed.c" "$output_dir/program.c"
"$output_dir/host"
ok
current='successful compiler diagnostics and closed standard input'
cat > "$output_dir/warning" <<'SH'
#!/bin/sh
if read -r ignored; then exit 9; fi
printf 'compiler warning on success\n'
exec "$REAL_CC" "$@"
SH
chmod +x "$output_dir/warning"
REAL_CC="$compiler" CC="$output_dir/warning" "$interpreter" --save-library "$output_dir/program.goat" > "$output_dir/stdout.txt" 2> "$output_dir/stderr.txt"
test ! -s "$output_dir/stdout.txt"
grep -q 'compiler warning on success' "$output_dir/stderr.txt"
"$output_dir/host"
ok
current='literal filenames and compiler paths, no shell evaluation'
# Dollar signs, quotes, spaces and semicolons must remain part of literal paths.
strange='name ;touch INJECTED; $(touch INJECTED) "quote"'
mkdir -p "$output_dir/$strange"
cp "$output_dir/program.goat" "$output_dir/$strange/program.goat"
cat > "$output_dir/compiler ; with spaces" <<'SH'
#!/bin/sh
exec "$REAL_CC" "$@"
SH
chmod +x "$output_dir/compiler ; with spaces"
REAL_CC="$compiler" CC="$output_dir/compiler ; with spaces" "$interpreter" --save-library "$output_dir/$strange/program.goat"
test -s "$output_dir/$strange/program.so"
test ! -e INJECTED
cp "$output_dir/program.goat" "$output_dir/-leading.goat"
(cd "$output_dir" && CC="$compiler" "$interpreter" --save-library ./-leading.goat)
test -s "$output_dir/-leading.so"
ok
current='compiler start failure preserves previous library'
cp "$output_dir/program.so" "$output_dir/original.so"
expect_failure() {
    if CC="$1" "$interpreter" --lang en --save-library "$output_dir/program.goat" > "$output_dir/stdout.txt" 2> "$output_dir/stderr.txt"; then false; fi
    test ! -s "$output_dir/stdout.txt"
    test -s "$output_dir/stderr.txt"
    cmp "$output_dir/program.so" "$output_dir/original.so"
    clean
}
expect_failure "$output_dir/missing-compiler"
grep -q 'cannot start compiler' "$output_dir/stderr.txt"
# CC is an executable, not a shell command or a list of compiler flags.
expect_failure "$compiler -O3"
ok
current='compiler diagnostics and nonzero exit'
cat > "$output_dir/fail" <<'SH'
#!/bin/sh
printf 'compiler stdout\n'
printf 'compiler stderr\n' >&2
exit 23
SH
chmod +x "$output_dir/fail"
expect_failure "$output_dir/fail"
grep -q 'compiler stdout' "$output_dir/stderr.txt"
grep -q 'compiler stderr' "$output_dir/stderr.txt"
grep -q 'exit 23' "$output_dir/stderr.txt"
ok
current='compiler terminated by signal'
cat > "$output_dir/signal" <<'SH'
#!/bin/sh
kill -TERM $$
SH
chmod +x "$output_dir/signal"
expect_failure "$output_dir/signal"
grep -q 'signal 15' "$output_dir/stderr.txt"
ok
current='missing compiler artifact and bounded diagnostic capture'
cat > "$output_dir/no-artifact" <<'SH'
#!/bin/sh
exit 0
SH
chmod +x "$output_dir/no-artifact"
expect_failure "$output_dir/no-artifact"
grep -q 'file operation failed' "$output_dir/stderr.txt"
cat > "$output_dir/verbose" <<'SH'
#!/bin/sh
head -c 100000 /dev/zero | tr '\000' x
exit 2
SH
chmod +x "$output_dir/verbose"
expect_failure "$output_dir/verbose"
grep -q 'truncated at 65536' "$output_dir/stderr.txt"
test "$(wc -c < "$output_dir/stderr.txt")" -lt 67000
ok
current='empty or symlink compiler artifacts are rejected'
cat > "$output_dir/empty-output" <<'SH'
#!/bin/sh
previous=''
for arg do
    if [ "$previous" = '-o' ]; then : > "$arg"; fi
    previous="$arg"
done
SH
chmod +x "$output_dir/empty-output"
expect_failure "$output_dir/empty-output"
cat > "$output_dir/symlink-output" <<'SH'
#!/bin/sh
previous=''
for arg do
    if [ "$previous" = '-o' ]; then ln -s module.c "$arg"; fi
    previous="$arg"
done
SH
chmod +x "$output_dir/symlink-output"
expect_failure "$output_dir/symlink-output"
ok
current='real compiler rejects invalid C and unresolved symbols'
cat > "$output_dir/invalid" <<'SH'
#!/bin/sh
for arg do
    case "$arg" in */module.c) printf 'this is invalid C\n' > "$arg";; esac
done
exec "$REAL_CC" "$@"
SH
chmod +x "$output_dir/invalid"
export REAL_CC="$compiler"
expect_failure "$output_dir/invalid"
grep -qi 'error' "$output_dir/stderr.txt"
cat > "$output_dir/unresolved" <<'SH'
#!/bin/sh
for arg do
    case "$arg" in */module.c) printf 'extern int missing(void); int f(void){return missing();}\n' > "$arg";; esac
done
exec "$REAL_CC" "$@"
SH
chmod +x "$output_dir/unresolved"
expect_failure "$output_dir/unresolved"
grep -q 'missing' "$output_dir/stderr.txt"
ok
current='atomic replacement protects symlink and hardlink targets'
cp "$output_dir/program.goat" "$output_dir/alias.goat"
cp "$output_dir/program.goat" "$output_dir/source-copy.goat"
ln -sf alias.goat "$output_dir/alias.so"
CC="$compiler" "$interpreter" --save-library "$output_dir/alias.goat"
cmp "$output_dir/alias.goat" "$output_dir/source-copy.goat"
test ! -L "$output_dir/alias.so"
rm "$output_dir/alias.so"
ln "$output_dir/alias.goat" "$output_dir/alias.so"
CC="$compiler" "$interpreter" --save-library "$output_dir/alias.goat"
cmp "$output_dir/alias.goat" "$output_dir/source-copy.goat"
ok
current='publish failure and source protection'
cp "$output_dir/program.goat" "$output_dir/blocked.goat"
mkdir -p "$output_dir/blocked.so"
if CC="$compiler" "$interpreter" --save-library "$output_dir/blocked.goat" > "$output_dir/stdout.txt" 2> "$output_dir/stderr.txt"; then false; fi
test -d "$output_dir/blocked.so"
cp "$output_dir/program.goat" "$output_dir/source.so"
if "$interpreter" --save-library "$output_dir/source.so" > "$output_dir/stdout.txt" 2> "$output_dir/stderr.txt"; then false; fi
cmp "$output_dir/source.so" "$output_dir/program.goat"
ok
current='invalid options and parser errors never invoke compiler'
cat > "$output_dir/spy" <<'SH'
#!/bin/sh
touch "$SPY_MARKER"
exit 1
SH
chmod +x "$output_dir/spy"
export SPY_MARKER="$output_dir/invoked"
for flag in '--optimize none' --print-bytecode --print-analysis --print-source-code; do
    read -r -a args <<< "$flag"
    if CC="$output_dir/spy" "$interpreter" --save-library "${args[@]}" "$output_dir/program.goat" > "$output_dir/stdout.txt" 2> "$output_dir/stderr.txt"; then false; fi
done
printf 'var = ;\n' > "$output_dir/bad.goat"
if CC="$output_dir/spy" "$interpreter" --save-library "$output_dir/bad.goat" > "$output_dir/stdout.txt" 2> "$output_dir/stderr.txt"; then false; fi
test ! -e "$SPY_MARKER"
test ! -e "$output_dir/bad.so"
ok
current='empty module and concurrent compilations'
printf 'print("MUST NOT EXECUTE");\n' > "$output_dir/empty.goat"
CC="$compiler" "$interpreter" --save-library "$output_dir/empty.goat" > "$output_dir/stdout.txt"
test ! -s "$output_dir/stdout.txt"
cat > "$output_dir/empty-host.c" <<'C'
#include <goat/native_abi.h>
#include <assert.h>
int main(void) {
    const goat_native_module_v1_t *module=goat_native_query_v1(GOAT_NATIVE_ABI_VERSION);
    assert(module && !module->entry_count && !module->entries);
    return 0;
}
C
compile "$output_dir/empty-host.c" "$output_dir/empty.so" -o "$output_dir/empty-host"
"$output_dir/empty-host"
CC="$compiler" "$interpreter" --save-library "$output_dir/program.goat" &
first=$!
CC="$compiler" "$interpreter" --save-library "$output_dir/program.goat" &
second=$!
wait "$first"
wait "$second"
"$output_dir/host"
ok
printf 'Native library testing done; total: %d, passed: %d, failed: 0\n' "$passed" "$passed"
