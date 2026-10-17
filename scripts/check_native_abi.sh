#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
set -Eeuo pipefail
runtime_include="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../include/goat" && pwd)"
interpreter="${1:?goat executable required}"
output_dir="${2:?output directory required}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
compiler_flags=("${@:3}")
mkdir -p "$output_dir"
passed=0
current='exported support headers match the public library'
printf 'Starting native ABI testing...\n'
trap 'printf "[fail] %s\nNative ABI testing done; total: %d, passed: %d, failed: 1\n" "$current" "$((passed+1))" "$passed"' ERR
ok() { printf '[ ok ] %s\n' "$current"; passed=$((passed+1)); }
compile() { "${CC:-gcc}" -I"$runtime_include" -std=c11 -Wall -Wextra -Werror -pedantic-errors -O2 -I"$repo_root/include" "${compiler_flags[@]}" "$@"; }
cp "$repo_root/test/functional/native_abi/program.goat" "$output_dir/module.goat"
"$interpreter" --save-c "$output_dir/module.goat"
diff -b --strip-trailing-cr "$repo_root/include/goat/native_abi.h" "$output_dir/native_abi.h"
diff -b --strip-trailing-cr "$repo_root/include/goat/native_runtime.h" "$output_dir/native_runtime.h"
if grep -Eq 'static inline|typedef struct|ABI declarations begin' "$output_dir/module.c"; then false; fi

diff -b --strip-trailing-cr "$repo_root/include/goat/native_strings.h" "$output_dir/native_strings.h"
ok
current='separately compiled host calls numeric adapters'
compile -c "$output_dir/module.c" -o "$output_dir/module.o"
compile "$repo_root/test/functional/native_abi/driver.c" "$output_dir/module.o" -lm -o "$output_dir/driver.exe"
"$output_dir/driver.exe"
ok
current='exported C compiles using only its companion headers'
"${CC:-gcc}" -std=c11 -Wall -Wextra -Werror -pedantic-errors -O2 "${compiler_flags[@]}" \
    -c "$output_dir/module.c" -o "$output_dir/standalone.o"
ok
current='save-c preserves conflicting support headers'
mkdir -p "$output_dir/conflict"
cp "$output_dir/module.goat" "$output_dir/conflict/program.goat"
printf 'user-owned header\n' > "$output_dir/conflict/native_runtime.h"
cp "$output_dir/conflict/native_runtime.h" "$output_dir/conflict/original.h"
if "$interpreter" --save-c "$output_dir/conflict/program.goat" > "$output_dir/conflict/log" 2>&1; then false; fi
cmp "$output_dir/conflict/native_runtime.h" "$output_dir/conflict/original.h"
test ! -e "$output_dir/conflict/program.c"
ok
current='save-c preserves conflicting static libraries'
mkdir -p "$output_dir/archive-conflict"
cp "$output_dir/module.goat" "$output_dir/archive-conflict/program.goat"
printf 'user-owned archive\n' > "$output_dir/archive-conflict/libgoat_native.a"
cp "$output_dir/archive-conflict/libgoat_native.a" "$output_dir/archive-conflict/original.a"
if "$interpreter" --save-c "$output_dir/archive-conflict/program.goat" > "$output_dir/archive-conflict/log" 2>&1; then false; fi
cmp "$output_dir/archive-conflict/libgoat_native.a" "$output_dir/archive-conflict/original.a"
test ! -e "$output_dir/archive-conflict/program.c"
ok
current='empty module exposes an empty descriptor'
printf 'print(1);\n' > "$output_dir/empty.goat"
"$interpreter" --print-c "$output_dir/empty.goat" > "$output_dir/empty.c"
cat > "$output_dir/empty_driver.c" <<'C'
#include "goat/native_abi.h"
#include <assert.h>
int main(void) {
    const goat_native_module_v2_t *module=goat_native_query_v2(GOAT_NATIVE_ABI_VERSION);
    assert(module && !module->entry_count && !module->entries);
    return 0;
}
C
compile "$output_dir/empty.c" "$output_dir/empty_driver.c" -lm -o "$output_dir/empty.exe"
"$output_dir/empty.exe"
ok
current='handwritten adapter links a precompiled vendor archive'
manual="$repo_root/test/functional/native_abi/manual"
compile -c "$manual/vendor.c" -o "$output_dir/vendor.o"
"${AR:-ar}" rcs "$output_dir/libvendor.a" "$output_dir/vendor.o"
compile -c "$manual/adapter.c" -o "$output_dir/adapter.o"
compile "$manual/host.c" "$output_dir/adapter.o" "$output_dir/libvendor.a" -o "$output_dir/manual.exe"
"$output_dir/manual.exe"
ok
current='lazy allocator lifetime, alignment and concurrent instances'
thread_flags=()
if [[ ${OS:-} != Windows_NT ]]; then thread_flags=(-pthread); fi
host_sources=("$repo_root/src/model/native_services.c" "$repo_root/src/lib/allocate.c")
compile -DMEMORY_DEBUG -I"$repo_root/src" "${host_sources[@]}" "$repo_root/test/functional/native_allocator/driver.c" "${thread_flags[@]}" -o "$output_dir/allocator.exe"
"$output_dir/allocator.exe"
for mode in overflow corrupt; do
    if "$output_dir/allocator.exe" "$mode" > "$output_dir/$mode.log" 2>&1; then false; fi
    if [[ "$mode" == corrupt ]]; then
        grep -q 'Memory corruption detected' "$output_dir/$mode.log"
    else
        grep -q 'Allocation size overflow' "$output_dir/$mode.log"
    fi
done
"$output_dir/allocator.exe" report > "$output_dir/accounting.log" 2>&1
grep -q 'native_services.c' "$output_dir/accounting.log"
# Also cover the release accounting path under concurrent allocations.
compile -I"$repo_root/src" "${host_sources[@]}" "$repo_root/test/functional/native_allocator/driver.c" "${thread_flags[@]}" -o "$output_dir/allocator-release.exe"
"$output_dir/allocator-release.exe"

ok
current='nested calls share memory through success and stack-limit recovery'
sed 's/(void)g_context;/goat_native_allocate(g_context, 17);/' "$output_dir/module.c" > "$output_dir/instrumented.c"
compile -DMEMORY_DEBUG -I"$repo_root/src" "${host_sources[@]}" -I"$output_dir" "$repo_root/test/functional/native_allocator/context.c" -lm -o "$output_dir/context.exe"
"$output_dir/context.exe"
ok
printf 'Native ABI testing done; total: %d, passed: %d, failed: 0\n' "$passed" "$passed"
