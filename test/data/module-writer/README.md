# Deterministic module writer migration (#367)

## Operator image validation correction (#504)

The independent function-image fixture previously used `NOP` at instruction 3,
allowing top-level execution to fall through into the function-owned `NIL; RET`
body, including variants with appended export-declaration tails. Instruction 3
is now `END`; function entries, ownership, native payloads,
and metadata tests are unchanged. This repairs an invalid independent format
fixture, not a frozen compiler-produced executable.

The authoritative recipe is `scripts/check_module_writer.py::function_image`
at Git revision `0c4de46b`. Canonical metadata, significant hashes and compressed
output archives were recomputed using that revision's independent
`docs/module-format-v1/reference.py` and `schema.py`, with zlib **1.3.2**.
Each original archive was reconstructed byte-for-byte before modification.
Input ZIP metadata/order and all unaffected payload bytes were preserved.
Content-addressed filenames, manifest entries, references and displayed expected
hashes were updated together. Intentionally invalid metadata/native inputs retain
their original defects and expected failure statuses. No production writer was
used to generate expected bytes, and no assertion was weakened.


Current #504 Debug verification: all 59 writer cases, all 133 inspector cases,
and 194 of 196 merge cases pass. Both merge socket cases fail before invoking
the tool because `socket(AF_UNIX, SOCK_STREAM, 0)` returns `EPERM` in this
execution environment; they were not skipped or counted as passes. All four
frozen-sample integrity/self-test aliases pass. All three corpus SHA-256 manifests
verify. These are local results, not claims about other platforms or Release.

The historical migration/parity results below describe the original corpus;
they are not new validation results for these corrected bytes.

The corrected full-native profile SHA-256 is
`b84d1cdbbbbd0583f47789f466841b853b47ed39e64a1c3feb77f58eda230e2f`.
The six positive native cases and five related negative inputs were repaired;
all seeded payloads/permutations and the empty-format golden remain unchanged.
The current corpus contains 51 blobs totaling 2,223,744 bytes.

`test/src/functional/module_writer_scenarios.c` replaces
`scripts/check_module_writer.py` at baseline
`0c4de46b` with **59 independently selectable CTest IDs**, label
`module-writer-migration`. [Complete scenario map](cases.md).
There are 34 memory cases, 24 publication cases and one API case.
The original #367 migration changed no production code, schema or existing
golden/sample file. The #504 correction above updates its independent fixtures.

## Fixture provenance and independent expectations

`scenarios.json` records the original argument combinations, allowed status sets,
limits, exact input archive bytes and expected parts. `blobs/` contains **51 unique
content-addressed files, 2,223,744 bytes total**: 28 unique inputs, four complete
expected output archives and shared canonical metadata/bytecode/native parts.
`SHA256SUMS` covers every blob, and the C reader verifies each blob against its
SHA-256 filename before use. Test data are read-only; no update mode is provided.

Inputs were captured while the unchanged original Python assertions ran. Expected
part dictionaries were computed by the original **independent**
`reference.normalize`/`reference.canonical` implementation in `expected_parts()`,
not extracted from the production output. Each positive case was then replayed
on its exact frozen input, with its part dictionary independently recomputed and
compared to the stored parts. Output archive bytes were captured only after the
original independent ZIP-profile/part checks passed. The empty archive additionally
compares the existing `docs/module-format-v1/vectors/empty.gbin`; the full 12-native
archive additionally requires the original frozen SHA-256
`98617e59853dee57926692a6698f67c341eb69e9cc118e7b55ec746f3a6c5102`.
The four complete output blobs are new test fixtures, not modified format goldens.

The C oracle independently checks ZIP entry order (including lexical
`variant-10` before `variant-2`), deflate method, UTF-8 flag, epoch date/time,
Unix creator/version, extract version, file attributes, absent extras/comments,
local/central CRC and size fields, absence of data descriptors, and the exact
EOCD/no-comment trailer. Independent extraction verifies CRC and compares every
part with the reference-derived bytes. It also checks unchanged significant hash,
normalization to the golden archive, information/author ordering, all native
permutations and renamed-input idempotence, and tied compiler/payload associations.
Invalid supplied metadata, hashes and parts go unchanged to the real writer;
no production oracle repairs or generates expected input/results.

The original seeded 65,537-byte payload and 12-index permutation are preserved
inside these exact inputs. Their pre-existing files/manifests/exporters under
`python-samples` remain unchanged. Historical source paths in those manifests
are provenance, not live imports of the retired runner. The optional original-AST
audit reads its explicitly selected Git baseline, not a current working-tree file.

## Publication and fault contracts

Every publication ID creates its own real destination beneath its scenario work
area. New publication, permission-preserving replacement, old hard-link identity,
and read-all-before-same-output replacement remain real filesystem operations.
Each invalid-input/limit/fault case starts with a valid full native archive,
requires the original failure status, empty failed stdout, unchanged old bytes
and mode, and exactly the same directory entries (no leaked temporary files).
Missing-parent/directory destinations, symlink-target preservation and POSIX FIFO
identity are separate scenarios. A publication subdirectory excludes framework
output artifacts from the temporary-file comparison.

All five test-host fault injections remain: write, close, validation, prepare and
rename. An additional test-only Release dependency was necessary:
`module_writer_fault_testing.c` had its initial partial `FWRITE` inside an assert
before the included ordinary host undefines `NDEBUG`. It now undefines `NDEBUG`
before its own headers. The write fault positively requires one byte written,
checks `FTELL == 1`, and emits `injected partial write: 1 byte`; the C scenario
requires that marker, including in Release. Production libraries and fault hooks
are unchanged; existing ordinary writer-host assertions were already preserved.

## Platforms and resource rules

Linux x86-64/AArch64, glibc/musl and Windows MinGW i686/x86-64/UCRT share the suite.
Windows uses strict UTF-8 argument adaptation and native wide hard/symbolic links;
only symlink error **1314** permits that ID to return skip 77. FIFO creation is
explicitly skipped on Windows because it is POSIX-only. No other platform error
is converted to a skip. Unix verifies mode `0640`, hard-link identity through
retained old bytes, and the FIFO's actual file type.

Memory cases have a 30-second shared deadline; API/publication cases allow 120
seconds for their several original 30-second commands. CTest allows 150 seconds
for process cleanup. Unique workspaces, full output diagnostics, failure retention
and `--keep-work` use shared scenario/process helpers. Child cleanup failures fail.
All Unix/MinGW build commands, explicit Release target lists, musl target lists,
and Release/musl label filters include the new runner and both host dependencies.

## Reproduction

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target module_writer_scenarios --parallel 4
ctest --test-dir build -L '^module-writer-migration$' --output-on-failure --parallel 4
build/module_writer_scenarios integration.module_writer.publication.fault-write \
  build/module_writer_testing build/module_writer_fault_testing \
  build test/data test/data docs/module-format-v1/vectors --keep-work
(cd test/data/module-writer && sha256sum -c SHA256SUMS)
```

Use `--list` instead of the ID to enumerate all 59 cases. Repeat with Release.
The explicit **local-only** `--allow-unsupported-acl` argument may be added to a
direct invocation. Only writer IO status 8 with `FS 5` in replacement/hardlink/
same-output permits that invocation to skip (77), after checking unchanged bytes,
mode and directory. It is never set by CTest or CI. Strict execution is the default.

## Verification before retirement

- C: **59/59 strict Debug, Release and UBSan**, plus **59/59 in a fresh Release
  build** built using only the new target and its declared dependencies.
- Original Python: **59 writer calls + API smoke**, strict Debug and Release,
  using `TMPDIR=$PWD/build/<configuration>` to use the same filesystem as C.
- Exact-input parity: all **34 inputs/status/output combinations** replayed with
  original Python in Debug/Release; all **20 positive** reference-derived part maps
  independently recomputed and original full ZIP profile assertions passed.
- The initial original run using default `/tmp` failed at call 36 with writer
  status 8 / FS 5: that filesystem lacks required ACL transfer. Its explicit
  local-only mode passed 56 checks. This is not a writer discrepancy: the same
  original strict suite passes 59 on the build filesystem. The C local opt-in
  was separately tested with `/tmp` and correctly returned 77; strict CI never
  enables it. No strict C test is skipped on the tested Linux build filesystem.
- Twelve semantic negative controls rejected in Debug/Release: wrong allowed
  status, normalized bytes, reference canonical part, native entry order, SHA,
  permutation relation, ZIP attributes with *both* captured bytes and hash altered,
  missing partial-write proof, wrong rename fault status, corrupted old destination,
  leaked temporary file, and lost replacement mode. Each reached its intended
  semantic assertion, not an unrelated setup failure.
- The original frozen-sample exporter check still passes after runner retirement:
  11 fixtures / 6 original sources, with no sample or provenance changes.
- The blob manifest passed before and after final runtime tests. Style and
  `git diff --check` passed. Adjacent validation regression remains green.

Unrelated full local CTest was not rerun; known environment-specific ACL/process
limitations elsewhere are outside this change. Exact-head GitHub workflows supply
full cross-platform evidence, linked in the implementation PR. No next migration
starts before that CI is green and the individually approved merge is verified.
The retired runner has no live imported helpers; shared reference/sample tools
remain for their other consumers and final cleanup tasks #394/#395.
