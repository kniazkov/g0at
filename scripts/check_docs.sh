#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
# Embedded examples must remain executable members of the normal analysis suite.
# Doxygen checks that the included files exist; this checks their test registration.
example_count=0
for page in docs/internals/en/*.md docs/internals/ru/*.md; do
    while IFS= read -r example; do
        name="${example%.goat}"
        if ! test -s "test/data/analysis/$name.expect" \
            || ! grep -Fxq "$name" test/data/analysis/list.txt; then
            printf '%s: analysis example %s has no expectations or test registration\n' \
                "$page" "$example" >&2
            exit 1
        fi
        example_count=$((example_count + 1))
    done < <(sed -n 's/^<!--! \\include{lineno} \([a-zA-Z0-9_-]*\.goat\) -->$/\1/p' "$page")
done
if [ "$example_count" -eq 0 ]; then
    printf 'No registered analysis examples found in the internal guides\n' >&2
    exit 1
fi
printf 'Checked %s embedded analysis example references\n' "$example_count"
doxygen --version
mkdir -p build/doxygen-check
doxygen scripts/Doxyfile.check
