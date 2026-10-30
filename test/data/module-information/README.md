# Information fields and merge migration (#364)

`module_information_scenarios` replaces `check_module_information.py` and
`check_module_information_merge.py`. Source:
`test/src/functional/module_information_scenarios.c`. Read-only inputs:
`information.json` and `information_merge.json` in this directory.

Baseline: `305ae938fe52c87c8afdfc443d15b3d7cd989868`. The original scripts ran with
all their Python assertions enabled while recording each exact binary stdin and
validated expected output. No Python is invoked by the C runner. Inputs retain
literal/escaped Unicode, NUL, invalid UTF-8, author/producer order, all malformed
values and all interactive response sequences. Writer ZIP input bytes (including
irrelevant input timestamp headers) are frozen; the deterministic production
writer output is checked separately, not regenerated from those input headers.

## Mapping

[Every stable ID, original source lines and host-call count](cases.md) is listed
in the complete map. There are **99 information IDs** (98 parameter groups plus
one symbol audit) and **107 merge IDs**. The 570 reader/writer invocations plus
one `nm` scan match the original **331 + 240** checks. Parameter suffixes are
zero-based in the original loop order; grouping retains related operations in
one independently selectable scenario rather than splitting identity checks
across processes.

| Original checks | C ID family (prefix `integration.module_`) |
| --- | --- |
| Linked executable has no network entry points | `information.no-network-symbols` |
| Significant hash; empty fields disappear and writer equals golden archive | `information.base-hash`, `information.empty-writer.*` |
| gmod/module empty, field type, Unicode/NUL/URL-like texts and escaped equivalence | `information.{gmod,module}.{empty,field.*,text.*}` |
| Canonical information overlay, hash invariance, writer bytecode and idempotence | `information.module.text.*` (gmod canonical overlay in `gmod.text.*`) |
| Author type/shape, exact order, empty email, decoded duplicates and malformed encoding | `information.{gmod,module}.author-*`, `.empty-email`, `.escaped-author-duplicate`, `.malformed-utf8.*` |
| Executable metadata/unknown properties still rejected by projection | `information.{gmod,module}.{invalid-version,unknown-property}` |
| Scalar records, producer validation, forbidden gmod producers and extra properties | `information.scalar-record.*`, `.producer-type.*`, `.producer-missing.*`, `.producer-{timestamp,machine,duplicate}`, `.gmod-producers`, `.escaped-producer-duplicate` |
| Producer canonical order/dedup identity, per-field hash invariance, text distinctions | `information.producer-order-idempotence`, `.field-hash.*`, `.text-distinction.*` |
| Empty merge inputs, empty/malformed fields, scalar agreement and conflicts | `information_merge.{zero-inputs,one-empty,three-empty,empty-field.*,text.*,conflict.*,decoded-text}` |
| Author preservation/conflicts, immediate EOF and explicit resolution | `information_merge.{author-order.*,empty-email,author-conflict.*,interactive-order,interactive-one}` |
| Eleven invalid responses, failure then successful retry, one menu/two prompts | `information_merge.interactive-retry.00`–`.10` |
| Multiple retries, whitespace, >300-byte line, identity and escaped display | `information_merge.interactive-{multiple-retry,spaces,long-line}`, `.author-identity-display` |
| Invalid author/producer records | `information_merge.invalid-author.*`, `.invalid-producer.*`, `.producer-type.*`, `.producer-missing.*` |
| Six producer-source permutations, union, decoded component identity | `information_merge.producer-permutation.00`–`.05`, `.producer-dedup`, `.authors-and-producers`, `.producer-identity.*`, `.decoded-producer` |
| Late invalid inputs and scalar conflicts must precede interactive menu | `information_merge.late-invalid.*`, `.late-invalid-before-conflict`, `.conflict-before-interaction` |

JSON comparisons use the independent `format_json` oracle: string lengths,
embedded NUL and array order are significant. The C runner also retains explicit
relations between outputs: escaped/literal projection equality, canonical
base-plus-information overlay, packed archive idempotence, and text inequality.
Every hash is cross-checked against the independent `format_binary_main_hash`
oracle with the unchanged schema and golden bytecode. ZIP entries are extracted
using the independent oracle, bytecode is unchanged, and empty information output
must equal the repository's exact `empty.gbin` golden bytes.

Merge stderr is compared exactly (CRLF normalized), additionally retaining menu
occurrence counts and forbidden pre-validation prompts. Failed reader operations
must emit no stdout; failed merge operations must also emit stderr. The symbol
audit runs the configured `CMAKE_NM -a` against the standalone production-linked
reader (not the VM), strips leading underscores, optional `imp_` import thunk
prefixes and `@N` suffixes, and rejects the same socket/DNS/WinHTTP/Internet/curl
entry points as the original. There are no platform skips or network requests.

## Run and reproduce parity

```sh
cmake -S . -B build/Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/Debug --target module_information_scenarios
ctest --test-dir build/Debug -L module-information-migration --parallel 4 --output-on-failure
build/Debug/module_information_scenarios \
  integration.module_information_merge.interactive-retry.00 \
  build/Debug/module_json_testing build/Debug/module_writer_testing /usr/bin/nm \
  build/Debug test/data test/data docs/module-format-v1/vectors --keep-work
```

Use `--list` instead of the ID for all IDs. Use the configured nm path on Windows,
not the sample Unix path. Each scenario owns a unique directory under build,
reads immutable data, shares a 120-second deadline, and retains logs/output on
failure; `--keep-work` also retains successful workspaces. Both corpus files are
CMake configure dependencies, so changed IDs update CTest registrations.

Repeat with Release. For UBSan use
`-DCMAKE_C_FLAGS='-fsanitize=undefined -fno-sanitize-recover=undefined'` and
`-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=undefined`. All existing Linux
x86-64/AArch64, Windows MinGW i686/x86-64/UCRT and musl Debug/Release selections
include the new target/label; remote CI verifies those platforms.

The migration exposed a necessary **test-harness-only** dependency:
`test/src/component/module_writer_testing.c` performed ZIP parsing and FWRITE
inside `assert`, but previously was not exercised in Release. With `NDEBUG`, both
the original Python checker and the C runner failed the first writer case
(`writer status 1`). The host now undefines NDEBUG before any headers, matching
other component hosts. Production libraries remain optimized Release code; no
production implementation, wire format, schema or golden data changed.

Retrieve both originals from the baseline, placing them under a temporary
`scripts/` directory whose parent exposes `docs/module-format-v1` (their ROOT
lookup is relative to `__file__`), then run against the same built reader/writer.
Before removal, both originals passed Debug and Release: **331 and 240 checks**.
The new runner passes **206/206** in both configurations and **206/206 under UBSan**. All negative controls
below were also run in both configurations. The implementation PR records validation logs and exact-head CI links.

## Negative controls

Use temporary copies of the corpus or a temporary proxy around the real host;
never modify canonical fixtures. The proxy must preserve stdin, exit status and
all unrelated output so rejection proves a semantic check, not setup failure.

1. Reverse expected authors in `information_merge.interactive-order`: JSON order
   assertion fails.
2. Reverse expected producers in `information.producer-order-idempotence`: JSON
   order assertion fails.
3. Append an extra `Choose author order` prompt to an invalid-response retry:
   stderr/menu contract rejects it.
4. Append a menu to `information_merge.late-invalid.00`: early-validation
   diagnostic contract rejects it.
5. Replace the significant hash output with 64 zeroes: exact hash check rejects it.
6. Flip a byte in an empty-information writer archive: exact/golden archive
   comparison rejects it.
7. A fake successful nm emits `0000 I __imp__WinHttpSendRequest@16`: the symbol
   normalization resolves `WinHttpSendRequest` and rejects it even on Linux.

Each returned 1 with a retained semantic assertion, not a missing file/executable
or command-launch error. Deliberate invalid inputs already present in the corpus
continue to test rejection and empty stdout independently of these controls.
