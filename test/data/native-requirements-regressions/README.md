# Supplemental native requirements regressions

These cases are independent of the frozen #379 migration corpus. They reuse its
PE32+ layout, without modifying its blobs, scenario table, or provenance hashes.
The host only inspects bytes, so these tests run on Linux and Windows.

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

## KERNEL32 `IsDBCSLeadByteEx`

PR #624 exposed this import in MinGW-generated native modules: both MINGW64
and UCRT64 rejected `KERNEL32.dll!IsDBCSLeadByteEx` before module execution.
[Microsoft documents this function](https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-isdbcsleadbyteex)
as a Kernel32 export available since Windows 2000. Accept its exact name in
the Windows 10 profile for both CRTs; unknown imports remain rejected.

The two additional fixtures derive from the frozen `pe-kernel-GetCPInfo` image:
`../native-requirements/blobs/d2e89aec9d0dfb4ceb42f7521771cb8cbc1b058b00fd08fdad4242ea52d786f0.bin`.
Replace 16 bytes at offset `0x742` with ASCII `IsDBCSLeadByteEx` plus NUL,
without resizing the image. This produces the MSVCRT fixture
`7b98d47317e74dd0953a36a8d07db7d8d5fe06e656921ad827145ac1b94c8d20.bin`.
For UCRT, additionally replace 13 bytes at offset `0x300` with ASCII
`ucrtbase.dll` plus NUL, producing
`b74b6ff834fa21de3260eca328729649bd1d0e72c1cbf6218ea17ee432854822.bin`.
Both 4096-byte fixtures require acceptance with the unchanged
`10.0.0.0 0.0.0.0` OS/runtime floor. The original migration corpus is unchanged.
