# Independent public inspector corpus and scenarios (#372)

`test/src/functional/module_inspect_scenarios.c` replaces
`scripts/check_module_inspect.py` with **133 independently selectable CTest IDs**:
`integration.module_inspect.text-001` through `text-070`, and `json-071` through
`json-133`, all labelled `module-inspect-migration`.

## Corpus provenance

`cases.json` freezes the original 133 invocation inputs at baseline
`cffff5f1010406733ee854e81ed5611f657da789`. Each record names its original source
line, stable ordinal, argv/environment, expected exit status, text contains/absent
checks, and complete portable input-file snapshot. `@ROOT@` is substituted with
the scenario's unique build-local workspace; `{ "hex": "…" }` encodes deliberately
invalid UTF-8 OS-boundary bytes. The unrelated working directory remains
`@ROOT@/unrelated`, and inherited `GOAT_MODULE_PATH` is cleared except in explicit
environment tests.

The 72 SHA-256-addressed blobs preserve the original independent Python/reference
archive construction (including external declarations and significant hashes).
Only ZIP wall-clock timestamps are fixed to 1980-01-01. Every blob is verified
against its content-addressed filename before use. `provenance.json` records the
original source/helper/vector hashes; `SHA256SUMS` permits independent corpus
verification. Neither a production writer nor a production validator generates
these inputs. No host-specific expected inspector reports are frozen.

**No compiled ELF/PE library is frozen.** Scenarios 060, 123 and 124 compile the
original observable constructor source with the real selected C compiler for the
current platform, hash the actual library, and package it with independent ZIP
helpers and the original metadata/bytecode templates. The target is deliberately
foreign (`minimumOs: [999]`), but explicitly selected. Windows changes only the
same OS/runtime/library suffix fields as the original script. The bad-hash case
retains a zero native hash. All three cases require that the constructor marker
remain absent. Looping bytecode is separately inspected without execution.

## Old checks → stable CTest suffixes

Ranges below include both endpoints. Prefix every suffix with
`integration.module_inspect.`. Exact individual argv/status/input snapshots are
in `cases.json`; cases never depend on another CTest process's output or files.

| Original scenario / assertions | Suffixes |
| --- | --- |
| text baseline, identity, validation/loading state, deterministic bytes and archive immutability | `text-001`–`text-002` |
| literal unusual/extensionless/Unicode paths; missing/corrupt literal never appends `.gbin`; dash-prefixed input | `text-003`–`text-009` |
| help/version | `text-010`–`text-011` |
| seven invalid argument forms | `text-012`–`text-018` |
| nine invalid probe-timeout values, missing timeout operand, three valid spellings | `text-019`–`text-031` |
| informational producers/exports; sorted names, exact mutability/type display, escaped controls, input-order-independent text | `text-032`–`text-033` |
| nested DEL/C1/bidi/line-separator escaping | `text-034` |
| absent/empty exports | `text-035`–`text-036` |
| retained metadata under hash failure and deterministic error text; invalid bytecode; invalid fields | `text-037`–`text-040` |
| three malformed JSON values and four damaged ZIP forms | `text-041`–`text-047` |
| native metadata, foreign compatibility, unsupported ABI/CPU, unknown compiler, native hash failure | `text-048`–`text-053` |
| three invalid explicit targets; two invalid informational producer schemas | `text-054`–`text-058` |
| valid looping bytecode never executes; real native constructor never loads | `text-059`–`text-060` |
| diamond graph sharing/order, missing dependency, explicit/environment search, authoritative corrupt first match, wrong hash | `text-061`–`text-067` |
| two versions remain distinct, graph function reference success/failure, named-root cycle | `text-068`–`text-070` |
| separate versioned JSON report contract, deterministic serialization, arbitrary module version | `json-071`–`json-073` |
| literal paths, Unicode, dash separator and literal no-suffix fallback | `json-074`–`json-079` |
| seven invalid argument forms and trailing `--json` recognition | `json-080`–`json-087` |
| invalid UTF-8 argv with JSON first/last and invalid UTF-8 environment | `json-088`–`json-090` |
| JSON help/version, missing target and three invalid profiles | `json-091`–`json-096` |
| exact informational Unicode/control strings, normalized empty author email, producers, input-order-independent JSON | `json-097`–`json-098` |
| four empty optional fields omitted, exact integer 9007199254740993, significant spaces, three invalid metadata forms | `json-099`–`json-107` |
| retained metadata/hash failure, deterministic bytes, locale-independent diagnostic-code sequence | `json-108`–`json-111` |
| multiple invalid-field diagnostics; malformed JSON prerequisite-skips; damaged containers never invent hash failures | `json-112`–`json-118` |
| compatibility is not loading; native compiler provenance/absence; failed native hash makes compatibility not checked | `json-119`–`json-121` |
| looping bytecode and valid/invalid native constructor archives never execute | `json-122`–`json-124` |
| sorted shared graph, input-order-independent JSON, distinct paths to missing shared dependency, deterministic error bytes | `json-125`–`json-128` |
| dependency hash mismatch, named-root cycle, invalid function reference | `json-129`–`json-131` |
| missing/bad-hash edges suppress reference resolution: REFERENCE_SKIPPED without invented REFERENCE_INVALID or prerequisite field | `json-132`–`json-133` |

The 11 cross-invocation contracts replay their prerequisite snapshot **inside the
same scenario workspace**, then install the selected snapshot before comparing:
002↔001, 033↔032, 038↔037, 062↔061, 072↔071, 079↔078, 098↔097,
109↔108, 111↔110, 126↔125, 128↔127. Text pairs 002/038 compare both streams;
079 compares the whole parsed object; 111 compares the ordered diagnostic-code
sequence; the other pairs compare stdout bytes. Removing files absent from the
new snapshot preserves missing-dependency and no-suffix-fallback semantics.

## Shared and case-specific contracts

Every text invocation checks exact normal exit, strict UTF-8 in both streams,
required/forbidden substrings, an error diagnostic when failing, and absence of
raw terminal ESC. JSON invokes the existing independent
`inspect_report_parse`/`inspect_report_schema` oracle: recursive duplicate keys
and malformed UTF-8 are rejected, the published closed schema is checked, stdout
is the only report channel, and version/exit/success/error severity agree.

Additional common JSON checks preserve canonical base64, exact sorted export
names/mutability/type presence and values, null-metadata/export/native coupling,
and exact compatibility/loading object shapes. Case-specific checks retain
informational fields, exact wide integers (never round-trip through `double`),
metadata/error/skipped distinctions, graph ordering/paths, native compatibility
vs loading, and byte-identical deterministic reports. Every inspection that has
a `module.gbin` also verifies its bytes remain unchanged.

## Running

```sh
cmake -S . -B build/inspect-Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/inspect-Debug --target module_inspect_scenarios
ctest --test-dir build/inspect-Debug --output-on-failure -L module-inspect-migration
build/inspect-Debug/module_inspect_scenarios integration.module_inspect.json-103 \
  build/inspect-Debug/goat-inspect /usr/bin/cc build/inspect-Debug test/data \
  docs/module-format-v1 --keep-work
(cd test/data/module-inspect && sha256sum -c SHA256SUMS)
```

Repeat in Release. All contracts use scenario requirements, not NDEBUG-sensitive
assertions. The shared framework supplies the unique build-local workspace,
complete command/output diagnostics, cumulative deadlines and owned-child cleanup.
Normal cases allow 60 seconds (two original 30-second inspections for paired
cases); real constructor cases allow 90 seconds including compilation. Failure
retains evidence automatically; `--keep-work` retains successful work as well.
Crashes, timeout or process cleanup errors are never accepted as ordinary negative
inspector results.

Windows explicitly skips only json-088/089/090: its UTF-16 process boundary cannot
express invalid UTF-8 argv/environment byte sequences. Their IDs remain visible
with exit 77. Every other case is applicable on all existing Linux x86-64/AArch64,
musl and Windows MinGW32/MinGW64/UCRT64 jobs; platform selection is not inferred
from whether a test passed. No production inspector, VM, loader, language or
container-format semantics are changed.

## Migration validation

The unchanged Python baseline passed all 133 invocations in both Debug and
Release. The C replacement passed all 133 registered scenarios in Debug, Release
and UBSan. For both normal configurations, actual argv, exit status and complete
stdout/stderr matched the original after workspace-root normalization; all 130
non-constructor input snapshots were byte-identical. For each of the three dynamic
constructor cases, the original unchanged text/JSON/schema checks were also
replayed against the exact C-built archive and profile: identical streams,
unchanged inputs and no constructor marker. This avoids treating native compiler
output or independent JSON serialization as a frozen platform-specific oracle.

An independent negative-control pass rejected 39 deliberately wrong inspector
responses per configuration (78/78): exit/stream errors, invalid UTF-8, recursive
plain/escaped duplicate keys, schema/type and wide-integer corruption, omitted
metadata/graph/path/skip contracts, constructor markers, input modification and
non-deterministic raw JSON. Five positive proxy controls per configuration also
passed, confirming that failures were caused by the mutations rather than the
control transport.

Two additional no-execution controls deliberately hung the text and JSON loop
hosts: each failed at the shared 60-second deadline, with clean child teardown
and retained diagnostics. Total independent controls: 90/90.

Git treats content-addressed `blobs/**` as `-text` to prevent checkout-time newline
conversion. This includes the original multiline constructor C source: its SHA-256
covers the exact LF bytes, even when Windows uses `core.autocrlf=true`. The runner
continues to require exact hashes; it never normalizes fixture bytes to hide a
changed input. An isolated autocrlf checkout reproduces the missing-attribute
failure (constructor source 479 → 493 bytes, 14 inserted CR bytes), and with this
attribute all 72 blob hashes remain unchanged.
