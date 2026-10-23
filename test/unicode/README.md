# Portable scalar string fixture

`scalars.gbin.hex` is a full executable image independently generated with Python
`struct` and the UTF-32LE codec, not with the C writer under test. It contains empty,
ASCII, Cyrillic, supplementary, combining and embedded-zero strings. Its precise
bytes are consumed unchanged by the real C loader on Linux and Windows.

Regenerate: `python scripts/check_unicode.py --generate`.
Verify: `python scripts/check_unicode.py build/goat build/unicode`.

This fixture targets #250's transitional GOATST01 / GOATSC01 signatures, not #251's
final GOATBC01 container/section layout. It must be replaced when #251 lands.
