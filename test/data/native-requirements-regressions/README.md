# Supplemental native requirements regressions

These cases are independent of the frozen #379 migration corpus. They reuse its
PE32+ layout, without modifying its blobs, scenario table, or provenance hashes.
The host only inspects bytes, so both tests run on Linux and Windows.

## MSVCRT `_hypot`

MinGW64's `hypot` implementation imports `msvcrt.dll!_hypot`, Microsoft's
longstanding double-precision CRT spelling. The Windows 10 profile must accept
that exact export alongside `hypot`. This was observed in the Windows MINGW64
`integration.native_libm.binary-hypot` and `integration.native_functional.libm`
failures on PR #487. An invented `_hypot_future` export must still be rejected;
this is not a prefix or general underscore alias rule.

Both 4096-byte fixtures derive from the frozen `pe-msvcrt` image:
`../native-requirements/blobs/5527412f1595587e04b462b5c3362b2f9e3463a2006e12410da010e1e6adf482.bin`.
To reproduce each blob, zero bytes `[0x702, 0x742)`, then write the ASCII symbol
(`_hypot` or `_hypot_future`) and its NUL terminator at `0x702`. The import hint
at `0x700`, import descriptors, DLL names, and `GetLastError` import at `0x742`
remain unchanged. No production parser or compiler generates these fixtures.
Each filename is the SHA-256 of its bytes. Unrelated Linux dependency files from
the historical corpus are intentionally not copied into these PE-only cases.

The successful case requires the unchanged `10.0.0.0 0.0.0.0` OS/runtime floor.
The negative case requires the exact unknown-import diagnostic token.
