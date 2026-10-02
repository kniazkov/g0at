#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
build_dir="${BUILD_DIR:-$repo_root/build}"
mkdir -p "$build_dir"
build_dir="$(cd "$build_dir" && pwd)"

cmake -S "$repo_root/src" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER="${CC:-gcc}"
cmake --build "$build_dir" --target goat unit_testing analysis_testing functional_testing
cp "$build_dir/goat" "$repo_root/goat"
"$build_dir/unit_testing"
"$build_dir/analysis_testing" "$repo_root/test/analysis"
(
    cd "$repo_root/test/functional"
    "$build_dir/functional_testing" "$build_dir/goat" list.txt
)
printf '\nDone.\n'
