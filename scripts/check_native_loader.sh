#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
set -Eeuo pipefail
interpreter="$(realpath "${1:?goat executable required}")"
mkdir -p "${2:?output directory required}"
output_dir="$(realpath "$2")"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
compiler="$(command -v "${CC:-gcc}")"
ext=so
shared=(-shared -fPIC)
libs=(-ldl)
compiler_env="$compiler"
if [[ ${OS:-} == Windows_NT ]]; then
    ext=dll
    shared=(-shared -Wl,--exclude-all-symbols)
    libs=()
    compiler_env="$(cygpath -am "$compiler")"
fi
passed=0
current='build generated and handwritten providers'
printf 'Starting native loader testing...\n'
trap 'printf "[fail] %s\nNative loader testing done; total: %d, passed: %d, failed: 1\n" "$current" "$((passed+1))" "$passed"' ERR
ok() { printf '[ ok ] %s\n' "$current"; passed=$((passed+1)); }
compile() { "$compiler" -std=c11 -Wall -Wextra -Werror -pedantic-errors -I"$repo_root/include" -I"$repo_root/src" "$@"; }
cp "$repo_root/test/functional/native_abi/program.goat" "$output_dir/program.goat"
CC="$compiler_env" "$interpreter" --save-library "$output_dir/program.goat"
provider="$repo_root/test/functional/native_loader/provider.c"
compile "${shared[@]}" -DGOAT_NATIVE_BUILD "$provider" -o "$output_dir/provider.$ext"
compile "${shared[@]}" -DGOAT_NATIVE_BUILD -DNO_QUERY "$provider" -o "$output_dir/no-query.$ext"
cat > "$output_dir/dependency.c" <<'C'
#ifdef _WIN32
__declspec(dllexport)
#endif
int provider_dependency(void) { return 42; }
C
compile "${shared[@]}" "$output_dir/dependency.c" -o "$output_dir/dependency.$ext"
compile "${shared[@]}" -DGOAT_NATIVE_BUILD -DNEED_DEPENDENCY "$provider" "$output_dir/dependency.$ext" -o "$output_dir/needs-dependency.$ext"
mv "$output_dir/dependency.$ext" "$output_dir/dependency.hidden"
compile -DMEMORY_DEBUG "$repo_root/test/functional/native_loader/driver.c" \
    "$repo_root/src/model/native_library.c" "$repo_root/src/model/native_library_linux.c" \
    "$repo_root/src/model/native_library_windows.c" "$repo_root/src/lib/allocate.c" \
    "${libs[@]}" -o "$output_dir/loader-host"
ok
current='ABI rejection, metadata snapshots, descriptor ownership and actual unload'
(cd "$output_dir" && ./loader-host "$output_dir/program.$ext" "provider.$ext" \
    "$output_dir/no-query.$ext" "$output_dir/needs-dependency.$ext" "$output_dir/unloaded.txt")
ok
printf 'Native loader testing done; total: %d, passed: %d, failed: 0\n' "$passed" "$passed"
