#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
mkdir -p build
goat_binary="${GOAT_BINARY:-./goat}"
"$goat_binary" --save-graph build/fibonacci-analysis.svg example/fibonacci_analysis.goat > build/fibonacci-output.txt
python3 - <<'PY'
from pathlib import Path
from xml.etree import ElementTree as ET

assert Path('build/fibonacci-output.txt').read_text().strip() == '55'
root = ET.parse('build/fibonacci-analysis.svg').getroot()
text = ''.join(root.itertext())
assert 'C view:' in text and '(real)' in text and 'C=supported' in text
assert any(node.get('stroke') == 'forestgreen' for node in root.iter())
assert any(node.get('fill') == '#f2faf2' for node in root.iter())
PY
