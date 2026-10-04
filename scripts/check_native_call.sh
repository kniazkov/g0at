#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
set -Eeuo pipefail
interpreter="$(realpath "${1:?goat executable required}")"
tests="$(realpath "${2:?unit test executable required}")"
mkdir -p "${3:?output directory required}"
output_dir="$(realpath "$3")"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
compiler="$(command -v "${CC:-gcc}")"
compiler_env="$compiler"
ext=so
shared=(-shared -fPIC)
if [[ ${OS:-} == Windows_NT ]]; then
    ext=dll
    shared=(-shared -Wl,--exclude-all-symbols)
    compiler_env="$(cygpath -am "$compiler")"
fi
"$compiler" -std=c11 -Wall -Wextra -Werror -pedantic-errors -O2 -I"$repo_root/include" \
    "${shared[@]}" -DGOAT_NATIVE_BUILD "$repo_root/test/functional/native_call/provider.c" \
    -o "$output_dir/provider.$ext"
cp "$repo_root/test/functional/native_call/program.goat" "$output_dir/program.goat"
CC="$compiler_env" "$interpreter" --save-library "$output_dir/program.goat"
"$tests" --native-call-tests "$output_dir/provider.$ext" "$output_dir/program.$ext" "$output_dir/unloaded.txt"
