# Archive-local merge migration (#374)

## Compact frozen corpus (#504)

`cases.json` stores the same frozen expectations in a transparent JSON dictionary
format, not a compressed binary or a recipe that runs `goat-merge`. The original
2,915,551-byte file is represented by **208,444 bytes** (92.85% smaller). No input
blob, expected output blob, assertion, scenario ID, case field, array order,
snapshot entry or object-member order changed during this storage refactor.

The versioned envelope has four fields:

- `format`: exactly `module-merge-corpus-v1`.
- `entries`: 224 shared `[path, literal_entry_object]` pairs, in first-use order.
  Entry objects retain their original field order and all literal values.
- `snapshots`: 171 distinct ordered arrays of zero-based entry indices.
  A snapshot must not contain duplicate paths.
- `cases`: all 196 original case objects, in their original order. Only object
  values of `files`, `after` or `during` are replaced with a snapshot index.
  Other fields are copied literally, including absent versus explicit null.

There are **392 tree snapshots** (`196 files + 196 after`) and **399 total
`files`/`after`/`during` fields**. The seven `during` values are five JSON nulls
and two action strings, not trees. Expanded trees contain 22,634 ordered entries.
References have only two levels, case to snapshot to literal entry; recursive
references, inheritance, patch ordering and cycles are not part of the format.
Each expansion creates independent owning JSON values, including nested entry
objects, so constructor updates cannot mutate another case or snapshot.

`test/support/merge_corpus.c` expands the tables in the existing C scenario
runner. It validates even unused definitions, reference types/ranges, duplicate
keys/paths/IDs, nesting and required structural fields. A 64 MiB input limit and
an overflow-safe, pre-allocation 64 MiB owning-value/string budget bound repeated
reference expansion. The loader neither invokes production writers nor derives
expected hashes or outputs. Normal tests need only the checked-in compact JSON
and blobs, not Python, Git history or a generated file in the source tree.

Four C controls, included in the existing `module-merge-migration` label, check
order-sensitive round-trip and deterministic interning, malformed data and
expansion limits, deep-copy lifetime/isolation, and the complete frozen corpus.
The integrity control pins the SHA-256 of the original ordered, minified JSON:

`0701ea5f9d73658ea6c571673f65f72548dcb678fb3afe63f23044ad0df5ceea`

Its size is 2,146,312 bytes. That value was recorded from the pre-compaction
frozen document, independently of the compact representation. The original file
SHA-256 is `1205e915fbe0095db9e90dbbd4d62422188f0331e7e595a5e0e856704d2d12d7`;
its Git blob is `9e26e2b94fcb1d768d6cd30f19827d636299c5f1`. Provenance records
both forms and their exact counts. Whitespace is excluded from the ordered
JSON comparison; object and array order, JSON types and values are not.

### Reproduce conversion and equivalence

The existing runner also hosts C-only developer tools. `FROZEN` is a saved
pre-compaction corpus, or an explicitly reviewed expanded document when editing
fixtures. Packing verifies an order-sensitive round-trip before writing output.
The verification mode compares against the supplied frozen document, not against
any output produced by `goat-merge`:

```sh
runner=build/debug/module_merge_scenarios
"$runner" --corpus-pack "$FROZEN" build/cases.compact.json
"$runner" --corpus-verify "$FROZEN" build/cases.compact.json
"$runner" --corpus-expand test/data/module-merge/cases.json build/cases.expanded.json
sha256sum build/cases.expanded.json
"$runner" --corpus-pack build/cases.expanded.json build/cases.repacked.json
cmp test/data/module-merge/cases.json build/cases.repacked.json
ctest --test-dir build/debug -L merge-corpus --output-on-failure
ctest --test-dir build/debug -L module-merge-migration --output-on-failure
```

The corpus contains frozen expected results: legitimate changes still require
independent review of those expectations and an explicit integrity-hash update.
None of these commands automatically derives or updates that expected digest.

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

`cases.json` contains **196 independently selectable semantic scenario IDs**:
194 calls captured while every original assertion in `check_module_merge.py`
passed, plus the two unchanged Unix-socket contracts whose fixture creation is
not permitted in this local executor. [Complete scenario map](cases.md).
`case-ids.cmake` exposes precisely the same IDs for CTest registration.

## Independent data and expectations

Inputs were built by the original `pack`, `function_image`, `native_archive`,
`with_external_declarations` and `reference.main_hash` implementations. Only ZIP
wall-clock timestamps were fixed to 1980-01-01; malformed JSON, stale supplied
hashes, member ordering, exact Unicode/NUL text, bytecode and opaque native
payloads remain unchanged. The corpus contains 128 content-addressed blobs,
97,455 bytes total after the #504 correction, guarded by SHA256SUMS and runtime SHA checks.

The original `success()` assertions independently validated output normalization,
canonical metadata, significant hashes, native payload hashes, ordering,
compression and timestamps before expected output bytes were recorded. Original
case-specific metadata/author/compiler/policy assertions also ran unchanged.
All **72 stored expected outputs** were subsequently rebuilt independently using
`reference.normalize`, `canonical`, `main_hash` and `zip_bytes`; every entire
archive matched exactly. Golden cases additionally preserve `empty.gbin`.
Terminal outputs retain the original explicit author-order assertions and also
pass the same independent reference rebuild. C scenarios run their independent
format oracle in addition to exact expected-output comparison, not just a frozen
production-output comparison.

`provenance.json` records baseline/source hashes and validation provenance. No
ELF, PE, compiled DLL/SO or archive containing one is stored in the corpus.
`constructor_source`, metadata and bytecode are portable templates. The actual
constructor case invokes the selected real compiler, embeds its freshly compiled
payload, verifies unchanged input/code/payload, and requires the marker to remain
absent. Windows uses the original Windows/UCRT target and `.dll` suffix.

## Filesystem snapshots and platform boundaries

Each case has its own complete *relevant* before/after tree. Regular bytes,
directories, hard-link relationships, symbolic-link targets, FIFO/socket types,
selected permission checks and umask are preserved. A hard-linked old output
remains distinct from the new publication. Failure cases require unchanged
filtered siblings and contents; real terminal cases retain prompt barriers,
retries, EOF, frozen-source reads and destination-symlink substitution.

Original snapshots accumulate irrelevant artifacts from earlier cases. For
portable independent cases, unrelated FIFO/socket/symlink leftovers, the POSIX
literal-backslash filename and runtime constructor artifacts are removed from
both trees. They are retained in the cases that actually exercise them. No tested
argument, source, destination or observation is pruned. Constructor artifacts
are represented by explicit runtime entries in its own case. Permission assertions
remain specific to the original mode/umask cases, not accidental host defaults.

POSIX-only cases are explicit: literal backslash, mode/umask, FIFO/socket/device
and terminal contracts. Windows symbolic links may skip only the documented
privilege error1314; other errors must fail. Socket source/destination cases have
`local_original_executed:false`: the local strict original stopped at socket
creation with EPERM after119 checks. A documented rerun with ONLY
`--allow-restricted-sockets` passed194 checks in both Debug and Release, including
all ACL replacement, hard-link, permissions and PTY assertions. No ACL opt-out
was used. Both socket cases remain strict in C and in remote CI; a local absence
of permission is not reported as a test pass.

## Local parity and validation

The unchanged original passed **194/194 checks in Debug and Release** with only
the explicit restricted-socket opt-out. The C runner passed the same **194/194
cases in Debug, Release and UBSan**. The two socket cases fail strictly with
EPERM locally; the explicit local opt-out returns skip code77, not success.
Strict remote CI must execute them without this option.

The dynamic constructor was also compared separately in Debug and Release:
source, freshly compiled library and bytecode were byte-identical to the
original run, and parsed input metadata was identical. Input ZIP headers and
metadata serialization differ, so dynamic input archives are **not claimed to
be byte-identical**. The original unchanged `run`, `metadata` and `success`
helpers were then replayed against the exact retained C-generated input archive.
All original assertions passed; the output exactly matched the C output and an
independent `reference.zip_bytes` reconstruction. Input bytes remained unchanged
and no constructor marker appeared. This establishes same-input parity without
using native library bytes as a committed fixture.

The shared terminal owner also has 14 dedicated controls. Additional deliberate
fault-injection controls are reported with the PR validation results; they do
not replace the original positive cases or strict platform CI.

## Reproduce fixture verification

```sh
(cd test/data/module-merge && sha256sum -c SHA256SUMS)
```

The C runner uses shared deadlines, unique build-local workspaces, retained
binary output/diagnostics and `--keep-work`. The original Python runner was removed only after the complete strict remote
parity gate below. Unix-socket and terminal contracts remain registered.

## Exact checkout bytes on Windows

The first Windows CI passed the unchanged original runner and all other C cases,
but the constructor stopped at the fixture SHA256 check. Its plain-text C-source
blob was eligible for Git's automatic LF-to-CRLF checkout conversion. A local
`core.autocrlf=true` checkout reproduced the changed SHA256; the narrow
`.gitattributes` rule `test/data/module-merge/blobs/* -text` preserves all 128
content-addressed blobs byte-for-byte. This changes checkout policy, not test
expectations or constructor behavior. Debug/Release negative controls still
reject the CRLF-altered blob, while the byte-preserving checkout passes. All 128
checksums also pass after a forced autocrlf checkout with the rule applied.

## Completed strict remote parity gate

Head `9bceca18659f27ee95584100f94b24228e5af8e3` passed
[Build/test 11/11](https://github.com/kniazkov/g0at/actions/runs/41230302405)
and [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41230302471).
Direct job logs confirm 3,438 C passes: 196 Debug + 196 Release on six
Linux/musl jobs and 181 + 181 on three Windows jobs. The 90 skips are exactly
the 15 documented POSIX-only cases in both configurations on Windows; no other
case skipped. The unchanged original Python suite passed all 14 Debug/Release
runs on its seven existing glibc/Windows platforms. Only after this full green
gate were the original runner and its temporary validation registration removed.
The C sources and corpus are unchanged from that validated head.
