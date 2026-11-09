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
#include "lib/platform/windows_target.h"
#include <windows.h>
_Static_assert(_WIN32_WINNT == EXPECTED_TARGET, "Windows API target");
_Static_assert(WINVER == EXPECTED_TARGET, "Windows version target");
STARTUPINFOEXA startup;
DWORD flags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
C
# Discover Windows translation units so new test helpers cannot silently escape
# the low-API-target regression check. Keep backend files without direct Windows
# includes in the compilation set as well.
mapfile -t sources < <(
    {
        printf '%s\n' src/codegen/native_compiler.c lib/process/cleanup_service.c
        while IFS= read -r -d '' source; do
            if grep -q -E '^[[:space:]]*#[[:space:]]*include[[:space:]]*<windows\.h>' "$source"; then
                printf '%s\n' "$source"
            fi
        done < <(find lib src test tools -type f -name '*.c' -print0)
    } | LC_ALL=C sort -u
)
for target in 0x0501 0x0A00; do
    current="Windows API probe with initial target $target"
    expected=0x0A00
    flags=(-std=c11 -I. -Isrc -Isrc/module -Iinclude -Itest/src -Ithird_party/yyjson
           -D_WIN32_WINNT="$target" -DWINVER="$target" -Werror=implicit-function-declaration)
    "$compiler" "${flags[@]}" -DEXPECTED_TARGET="$expected" -c "$output_dir/probe.c" -o "$output_dir/probe-$target.o"
    printf '[ ok ] %s\n' "$current"
    passed=$((passed+1))
    for source in "${sources[@]}"; do
        current="$source with initial Windows API target $target"
        object="$output_dir/$target/${source%.c}.o"
        mkdir -p "$(dirname -- "$object")"
        "$compiler" "${flags[@]}" -c "$source" -o "$object"
        printf '[ ok ] %s\n' "$current"
        passed=$((passed+1))
    done
done
printf 'Windows API target testing done; total: %d, passed: %d, failed: 0\n' "$passed" "$passed"
