#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
build_dir="${BUILD_DIR:-$repo_root/build/release}"
mkdir -p "$build_dir"
build_dir="$(cd "$build_dir" && pwd)"

cmake -S "$repo_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="${CC:-gcc}"
cmake -P "$repo_root/scripts/build_command.cmake" "$build_dir" --target goat goat-inspect goat-merge module_delivery_scenarios package_scenarios frozen_samples format_vectors graph_scenarios module_book_scenarios module_delivery_requirements_scenarios module_musl_scenarios
cmake -P "$repo_root/scripts/build_command.cmake" "$build_dir" --target package --config Release
cmake "-DGOAT_BUILD_DIR=$build_dir" -P "$repo_root/scripts/publish_tools.cmake"
printf '\nDone.\n'
