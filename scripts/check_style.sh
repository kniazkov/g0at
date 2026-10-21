#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
formatter="${CLANG_FORMAT:-clang-format-18}"
version="$("$formatter" --version)"
if [[ ! "$version" =~ clang-format\ version\ 18\. ]]; then
    printf 'Expected clang-format 18, got: %s\n' "$version" >&2
    exit 2
fi
printf '%s\n' "$version"
# Reject invalid configuration before checking source formatting.
"$formatter" --style="file:$repo_root/.clang-format" --dump-config > /dev/null
mapfile -d '' -t files < <(git ls-files -z -- '*.c' '*.h' ':!:third_party/**')
if (( ${#files[@]} == 0 )); then
    printf 'No tracked C sources or headers found.\n' >&2
    exit 2
fi
printf 'Checking %d tracked C sources and headers.\n' "${#files[@]}"
for file in src/module/module_json.c src/module/module_json.h src/module/module_json_testing.c; do
  echo "BEGIN_FORMATTED:$file"
  "$formatter" --style="file:$repo_root/.clang-format" "$file"
  echo "END_FORMATTED:$file"
done
"$formatter" --style="file:$repo_root/.clang-format" \
    --dry-run --Werror --ferror-limit=0 "${files[@]}"
printf 'Code style check passed.\n'
