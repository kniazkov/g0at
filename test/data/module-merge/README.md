# Archive-local merge migration (#374)

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
97,424 bytes total, guarded by SHA256SUMS and runtime SHA checks.

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
