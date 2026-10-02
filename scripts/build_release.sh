#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
build_dir="${BUILD_DIR:-$repo_root/build/release}"
mkdir -p "$build_dir"
build_dir="$(cd "$build_dir" && pwd)"

cmake -S "$repo_root/src" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="${CC:-gcc}"
cmake --build "$build_dir" --target goat
cp "$build_dir/goat" "$repo_root/goat"
printf '\nDone.\n'
