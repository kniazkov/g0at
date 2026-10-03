#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
set -euo pipefail
unit_binary="${1:?unit_testing executable required}"
output_dir="${2:?output directory required}"
mkdir -p "$output_dir"
printf 'Starting C generation testing...\n'
if "$unit_binary" --emit-c-tests "$output_dir/generated.c" &&
   "${CC:-gcc}" -std=c11 -Wall -Wextra -Werror -pedantic-errors -O2 \
       "$output_dir/generated.c" -lm -o "$output_dir/generated.exe"; then
    printf '[ ok ] compile generated functions\n'
else
    printf '[fail] compile generated functions\nC generation testing done; total: 1, passed: 0, failed: 1\n'
    exit 1
fi
if "$output_dir/generated.exe"; then
    printf '[ ok ] execute generated functions\nC generation testing done; total: 2, passed: 2, failed: 0\n'
else
    printf '[fail] execute generated functions\nC generation testing done; total: 2, passed: 1, failed: 1\n'
    exit 1
fi
