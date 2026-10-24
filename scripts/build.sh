#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
set -euo pipefail

usage() {
    printf 'Usage: %s [jobs]\n' "$0"
    printf 'jobs: positive integer; default: CMAKE_BUILD_PARALLEL_LEVEL or logical CPU count.\n'
}
if [[ $# -eq 1 && ( $1 == --help || $1 == -h ) ]]; then
    usage
    exit 0
fi
if [[ $# -gt 1 ]]; then
    usage >&2
    exit 1
fi
build_jobs="${1:-${CMAKE_BUILD_PARALLEL_LEVEL:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf 1)}}"
if [[ ! $build_jobs =~ ^[1-9][0-9]*$ || ( $# -eq 1 && -z $1 ) ]]; then
    usage >&2
    exit 1
fi

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
build_dir="${BUILD_DIR:-$repo_root/build}"
mkdir -p "$build_dir"
build_dir="$(cd "$build_dir" && pwd)"

cmake -S "$repo_root/src" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER="${CC:-gcc}"
cmake --build "$build_dir" --parallel "$build_jobs" \
    --target goat unit_testing analysis_testing functional_testing module_json_testing child_process_testing cleanup_service_testing zip_testing module_container_testing module_bytecode_testing module_native_testing module_abi_testing module_consistency_testing module_validation_testing
cp "$build_dir/goat" "$repo_root/goat"
"$build_dir/module_json_testing"
"$build_dir/unit_testing"
"$build_dir/analysis_testing" "$repo_root/test/analysis"
(
    cd "$repo_root/test/functional"
    "$build_dir/functional_testing" "$build_dir/goat" list.txt
)
printf '\nDone.\n'
