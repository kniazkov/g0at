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
exe=
shared=(-shared -fPIC)
if [[ ${OS:-} == Windows_NT ]]; then
    ext=dll
    exe=.exe
    shared=(-shared -Wl,--exclude-all-symbols)
    compiler_env="$(cygpath -am "$compiler")"
fi
"$compiler" -std=c11 -Wall -Wextra -Werror -pedantic-errors -O2 -I"$repo_root/include" \
    "${shared[@]}" -DGOAT_NATIVE_BUILD "$repo_root/test/functional/native_pipeline/empty.c" \
    -o "$output_dir/empty.$ext"
"$compiler" -std=c11 -Wall -Wextra -Werror -pedantic-errors -O2 \
    "$repo_root/test/functional/native_pipeline/compiler.c" -o "$output_dir/invalid-compiler$exe"
cp "$output_dir/invalid-compiler$exe" "$output_dir/wrong-compiler$exe"
mkdir -p "$output_dir/temporary"
workspace_env="$output_dir/temporary"
wrong_env="$output_dir/empty.$ext"
if [[ ${OS:-} == Windows_NT ]]; then
    workspace_env="$(cygpath -am "$workspace_env")"
    wrong_env="$(cygpath -am "$wrong_env")"
fi
CC="$compiler_env" TMPDIR="$workspace_env" TMP="$workspace_env" TEMP="$workspace_env" \
GOAT_PIPELINE_WRONG_LIBRARY="$wrong_env" "$tests" --native-pipeline-tests "$compiler_env" \
    "$output_dir/invalid-compiler$exe" "$output_dir/wrong-compiler$exe" \
    "$repo_root/test/functional/native_pipeline/program.goat"
if [[ -n $(find "$output_dir/temporary" -mindepth 1 -print -quit) ]]; then
    echo 'Native pipeline left temporary files behind' >&2
    exit 1
fi
