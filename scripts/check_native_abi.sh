#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
set -Eeuo pipefail
interpreter="${1:?goat executable required}"
output_dir="${2:?output directory required}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
compiler_flags=("${@:3}")
mkdir -p "$output_dir"
passed=0
current='public and embedded ABI declarations agree'
printf 'Starting native ABI testing...\n'
trap 'printf "[fail] %s\nNative ABI testing done; total: %d, passed: %d, failed: 1\n" "$current" "$((passed+1))" "$passed"' ERR
ok() { printf '[ ok ] %s\n' "$current"; passed=$((passed+1)); }
compile() { "${CC:-gcc}" -std=c11 -Wall -Wextra -Werror -pedantic-errors -O2 -I"$repo_root/src" "${compiler_flags[@]}" "$@"; }
"$interpreter" --print-c "$repo_root/test/functional/native_abi/program.goat" > "$output_dir/module.c"
for file in "$repo_root/src/codegen/native_abi.h" "$output_dir/module.c"; do
    sed -n '/ABI declarations begin\./,/ABI declarations end\./p' "$file" | tr -d '[:space:]' > "$output_dir/abi-$(basename "$file").txt"
done
cmp "$output_dir/abi-native_abi.h.txt" "$output_dir/abi-module.c.txt"
ok
current='separately compiled host calls numeric adapters'
compile -c "$output_dir/module.c" -o "$output_dir/module.o"
compile "$repo_root/test/functional/native_abi/driver.c" "$output_dir/module.o" -lm -o "$output_dir/driver.exe"
"$output_dir/driver.exe"
ok
current='empty module exposes an empty descriptor'
printf 'print(1);\n' > "$output_dir/empty.goat"
"$interpreter" --print-c "$output_dir/empty.goat" > "$output_dir/empty.c"
cat > "$output_dir/empty_driver.c" <<'C'
#include "codegen/native_abi.h"
#include <assert.h>
int main(void) {
    const goat_native_module_v1_t *module=goat_native_query_v1(GOAT_NATIVE_ABI_VERSION);
    assert(module && !module->entry_count && !module->entries);
    return 0;
}
C
compile "$output_dir/empty.c" "$output_dir/empty_driver.c" -lm -o "$output_dir/empty.exe"
"$output_dir/empty.exe"
ok
printf 'Native ABI testing done; total: %d, passed: %d, failed: 0\n' "$passed" "$passed"
