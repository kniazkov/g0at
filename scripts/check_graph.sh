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
# The independent reference relies on assertions; never inherit optimization.
unset PYTHONOPTIMIZE
python3 - <<'PY'
from pathlib import Path
from xml.etree import ElementTree as ET

assert Path('build/fibonacci-output.txt').read_text().strip() == '55'
root = ET.parse('build/fibonacci-analysis.svg').getroot()
text = ''.join(root.itertext())
assert 'C view:' in text and '(integer)' in text and 'C=supported' in text
assert any(node.get('stroke') == 'forestgreen' for node in root.iter())
assert any(node.get('fill') == '#f2faf2' for node in root.iter())
assert Path('build/replacements-output.txt').read_text().strip() == 'result: 14'
root = ET.parse('build/replacements-analysis.svg').getroot()
text = ''.join(root.itertext())
assert 'expression replacement' in text and 'statement replacement' in text
assert 'original' in text and 'replacement' in text and 'if-else' in text
assert any(node.get('fill') == '#f5efff' and node.get('stroke') == 'purple' for node in root.iter())
PY
# Keep the Python reference above unchanged until the complete #393 parity gate.
graph_runner="${GRAPH_SCENARIOS_BINARY:-$(dirname -- "$goat_binary")/graph_scenarios}"
if [[ ! -x "$graph_runner" ]]; then
    echo "Build graph_scenarios and set GRAPH_SCENARIOS_BINARY to its path" >&2
    exit 1
fi
"$graph_runner" integration.graph "${GRAPH_BUILD_ROOT:-$repo_root/build}" \
    "$repo_root/test/data/graph" "$repo_root/build" "$@"
