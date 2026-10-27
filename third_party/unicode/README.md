# Unicode 17.0.0

Vendored Unicode Character Database files from
https://www.unicode.org/Public/17.0.0/ucd/ (also mirrored in
https://github.com/unicode-org/unicodetools/tree/main/unicodetools/data/ucd/17.0.0).
See LICENSE.txt (Unicode License v3) and 17.0.0/SHA256SUMS.

Build and run the [native generator](../../tools/unicode/README.md) with
`--update` to reproduce `lib/strings/unicode_data.h` and `17.0.0/SHA256SUMS`.
Use `--check` for a read-only freshness check. Builds use the committed tables without network access.
UnicodeData supplies simple mappings; unconditional SpecialCasing entries supply
full mappings; the language-independent Final_Sigma rule uses Cased and
Case_Ignorable from DerivedCoreProperties. Locale-specific rules are intentionally
excluded. White_Space comes from PropList. No normalization or case folding occurs.

The generated implementation-only header intentionally has neither `#pragma once`
nor an include guard: this is the sole agreed exception for project headers.
Its private table types are supplied by `unicode.c`; do not include it as a public API.
