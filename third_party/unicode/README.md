# Unicode 17.0.0

Vendored Unicode Character Database files from
https://www.unicode.org/Public/17.0.0/ucd/ (also mirrored in
https://github.com/unicode-org/unicodetools/tree/main/unicodetools/data/ucd/17.0.0).
See LICENSE.txt (Unicode License v3) and 17.0.0/SHA256SUMS.

Run `python scripts/generate_unicode.py` from any directory to reproduce
`src/lib/unicode_data.inc`. Builds use the committed tables without network access.
UnicodeData supplies simple mappings; unconditional SpecialCasing entries supply
full mappings; the language-independent Final_Sigma rule uses Cased and
Case_Ignorable from DerivedCoreProperties. Locale-specific rules are intentionally
excluded. White_Space comes from PropList. No normalization or case folding occurs.
