#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
# Doxygen 1.9.8 does not yet expand <!--! commands --> in Markdown.
# Keep one input line per output line so diagnostic locations remain correct.
set -euo pipefail
sed 's/^<!--! \(.*\) -->$/\1/' "$1"
