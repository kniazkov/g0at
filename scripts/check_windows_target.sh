#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
set -Eeuo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
output_dir="${1:?output directory required}"
mkdir -p "$output_dir"
compiler="${CC:-gcc}"
passed=0
current=''
printf 'Starting Windows API target testing...\n'
trap 'printf "[fail] %s\nWindows API target testing done; total: %d, passed: %d, failed: 1\n" "$current" "$((passed+1))" "$passed"' ERR
cat > "$output_dir/probe.c" <<'C'
#include "lib/windows_target.h"
#include <windows.h>
_Static_assert(_WIN32_WINNT == EXPECTED_TARGET, "Windows API target");
_Static_assert(WINVER == EXPECTED_TARGET, "Windows version target");
STARTUPINFOEXA startup;
DWORD flags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
C
for target in 0x0501 0x0A00; do
    current="native backend with initial Windows API target $target"
    expected="$target"
    if [[ "$target" == 0x0501 ]]; then expected=0x0602; fi
    flags=(-std=c11 -Isrc -Iinclude -D_WIN32_WINNT="$target" -DWINVER="$target" -Werror=implicit-function-declaration)
    "$compiler" "${flags[@]}" -DEXPECTED_TARGET="$expected" -c "$output_dir/probe.c" -o "$output_dir/probe-$target.o"
    "$compiler" "${flags[@]}" -c src/codegen/native_compiler_windows.c -o "$output_dir/compiler-$target.o"
    "$compiler" "${flags[@]}" -c src/model/native_library_windows.c -o "$output_dir/loader-$target.o"
    printf '[ ok ] %s\n' "$current"
    passed=$((passed+1))
done
printf 'Windows API target testing done; total: %d, passed: %d, failed: 0\n' "$passed" "$passed"
