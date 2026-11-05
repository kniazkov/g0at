# Frozen sample integrity gate (#394)

`frozen_samples` is a read-only C validator for the eleven committed
`test/data/python-samples` inputs and their existing `manifest.json`. It uses
SHA-256 from `lib/hash` and yyjson for syntax, never a production executable,
archive writer, module validator, or expected-result oracle. Fixture and manifest
bytes are unchanged by this migration.

```
build/frozen_samples --check --data test/data/python-samples
build/frozen_samples --self-test --data test/data/python-samples --build build
```

`--output DIR` aliases `--data DIR` for comparison with the former exporter.
There is no write/export mode. Only self-test writes, in an exclusively owned,
unique scenario directory below the existing build directory. `--keep-work`
preserves that directory; failures also retain it. Data/build overlap is rejected
by the scenario layer. Self-test has a 30-second cooperative deadline, and uses
runtime requirements, not `assert`, including in Release/NDEBUG builds.

## Integrity and provenance contract

The independently compiled anchor for the complete manifest is SHA-256
`a6a8db24abaf27ae8d15c4dc2c7c99b395ab5c3533ef25cdcdb87721a7fb1994`.
Pinning the entire manifest preserves its exact bytes, original baseline commit,
six historical source names/digests, scenarios, seeds, counts, fixture lengths and
digests, and all fully materialized damaged-input hashes. Reading trusted values
from an unanchored manifest would incorrectly accept coordinated fixture and
manifest edits; this runner explicitly rejects that case.

After checking the manifest anchor, the runner checks all eleven fixture names,
byte counts and SHA-256 values. It also reconstructs and hashes all 220 damaged
archives: 60 Unix ZIP, 60 Windows ZIP and 100 container inputs. Each recipe case
must contain exactly three ordered offset/XOR-mask pairs; offsets are bounded,
masks are one-bit bytes, and repeated offsets remain legal. All materialization
occurs in memory. Reads reject non-files and inputs larger than one MiB.

Historical source digests describe baseline
`89b8d80ba13dfa3cb5ca9d95b1448d45fec438f2`, not the migrated live source tree.
The old ordinary Python gate also stored these digests as constants; it did not
hash the live original source paths. No historical source file needs to exist
for the C integrity check.

## Deliberate reproduction boundary

The previous Python `--check` regenerated inputs with CPython Random, JSON and
zlib, then compared every generated fixture and manifest byte. The C gate
instead verifies the unchanged, independently pinned frozen corpus. It does not
reexecute those generators or check whether a new Python/zlib version reproduces
the same compressed bytes. This is a deliberate separation of frozen-input
integrity from optional recipe reproduction, not a claim that hashing reproduces
Python's random stream.

The four existing Python generation/audit helpers remain available but inactive
in normal CMake, CTest and CI gates until #395. Their optional original-history
audit and explicit reproduction commands remain documented in the corpus README.
The C gate requires neither Python nor Git history/network access.

## Negative controls

Self-test first verifies the supplied corpus, copies its twelve pinned files to
its workspace and validates the copy. For each fixture and the manifest, it
replaces the file with corrupt bytes and then removes it, requiring rejection
and checking that validation did not repair either state. It restores only its
own workspace copy. Finally it directly calls the damaged-input evaluator on a
one-bit-corrupted base for each of the three archive families, bypassing the
outer fixture gate so broken materialized digest comparisons cannot be hidden
by the fixture hash check. It restores the bases and revalidates both the copy
and the read-only source corpus. There are 24 file controls plus three direct
materialization controls.
