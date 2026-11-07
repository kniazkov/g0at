# Native integrity migration (#380)

Frozen source and constructor literals come byte-for-byte from
`scripts/check_native_integrity.py`; all three fixtures are read-only and marked
`-text`. Native work files reproduce Python text reads (strict UTF-8 and universal
newlines) and platform text writes. Captured Goat streams are strictly decoded;
compiler capture remains binary, as in the original.

The independent runner links the test-only `format_binary_sha256` oracle, never
production hashing or serialization. The digest is compared at bytes 64..95 of
`GOATPG01`. Pairing replaces these bytes, zeroes 56..63, calculates independent
64-bit FNV-1a over the complete artifact, and writes its little-endian checksum
at 56. No production helper constructs or repairs the expected artifact.

CLI: `native_integrity_scenarios ID GOAT BUILD DATA FIXTURES [--keep-work]`.
Each ID owns a new exclusive work directory. Goat invocations retain the original
30-second limit; the constructor compiler retains 60 seconds. Aggregate limit
is 360 seconds. Failures preserve work, streams and execution diagnostics;
`--keep-work` also retains successes. No fixture or shared data is modified.

## Original scenario mapping

Prefix: `native.native_integrity.`.

| ID | Exact assertions and observations |
| --- | --- |
| sha256-binding | Required compilation succeeds; magic and independent library SHA-256 binding match; required execution stdout is `42\n` |
| substitution | Same IDs/signature but body +2 library replaces +1; required mode rejects with empty stdout; auto exits zero and prints `42\n` |
| append-corruption | Append literal `changed`; same required rejection/empty stdout and successful auto fallback |
| preload-constructor | Compile other source, save C, append constructor; mismatched library rejects before marker creation; a rehashed legacy library reaches its constructor but fails module ABI validation; an independent OS loader proves that its constructor writes `loaded`; restore the original archive and observe `42\n` |

All scenarios establish the original digest and 42 baseline first. Substitution
and append-corruption each compile the other source before mutation, as in the
original. Constructor rejection intentionally does **not** constrain stdout;
initial/restored 42 checks intentionally do not add an exit-code predicate.
Ignored stderr remains ignored after decoding. Normal nonzero rejection remains
distinct from timeout/start/cleanup failure in the scenario framework.

Constructor compilation preserves inherited `CC` (default `gcc`) and exact
original flags: C11/O2/shared/native-build/math, Windows static/static-libgcc/
exclude-all-symbols or POSIX PIC. Compiler environment is inherited unchanged;
Goat receives English diagnostics and, only for constructor checks, its marker.
Marker absence checks any existing filesystem object, not just regular files.

## Local evidence

- Original Python passed in Debug, Release and UBSan, including actual constructor
  rejection, explicit pairing with result 43 and restoration to 42.
- C runner: Debug 1/4, Release 0/4, UBSan 0/4. Every failure is a normal child exit
  zero followed by supervisor `cleanup=2`; no oracle assertion was relaxed.
  This executor lacks `/proc/thread-self/children`. These are **not green runs**;
  full remote original/C parity was required and subsequently passed.
- Independent native-C fake observations: 94/94 controls across Debug and Release.
  Cases include bad magic/hash/status/output/UTF-8, generated-C decoding/newline normalization, rejected library status and
  stdout, auto fallback status/output, constructor pre-load marker, accepted
  status/output, missing/wrong positive marker, and restoration output. Positive
  controls also preserve ignored baseline status and ignored constructor-reject
  stdout. Each configuration additionally audits the paired bytes independently
  with Python hashlib and a separate FNV calculation (no production helper).
- Corpus extraction is byte-exact; format/diff checks passed. Original Python is
  removed only after the remote parity gate passed at
  `bc257368fade94723159b9596a363e431d44245c`. Parity and final CI evidence is in PR #458.
