#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
mkdir -p build
rm -f build/fibonacci-analysis.svg build/replacements-analysis.svg
goat_binary="${GOAT_BINARY:-./goat}"
"$goat_binary" --save-graph build/fibonacci-analysis.svg example/fibonacci_analysis.goat > build/fibonacci-output.txt
"$goat_binary" --save-graph build/replacements-analysis.svg example/replacements.goat > build/replacements-output.txt
graph_runner="${GRAPH_SCENARIOS_BINARY:-$(dirname -- "$goat_binary")/graph_scenarios}"
if [[ ! -x "$graph_runner" ]]; then
    echo "Build graph_scenarios and set GRAPH_SCENARIOS_BINARY to its path" >&2
    exit 1
fi
"$graph_runner" integration.graph "${GRAPH_BUILD_ROOT:-$repo_root/build}" \
    "$repo_root/test/data/graph" "$repo_root/build" "$@"
