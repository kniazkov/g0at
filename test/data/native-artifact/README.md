# Native artifact inspection migration (#380)

## Frozen independent corpus

All 25 original checks from `scripts/check_native_artifact.py` at
`9d3850c7da1dfee29497e446afa00aa7296f7a6f` are retained as independent stable IDs:
19 ELF/PE/export cases, five descriptor/metadata validations, and one real compiler
constructor-abort case. The original Python byte builders and mutation statements,
canonical descriptor JSON/envelope construction, metadata `json.dumps` serialization,
and C fixture source generation were executed unchanged against recording subprocess
boundaries. No production parser or current output generated expected values.
`provenance.json` records source/vector/corpus/constructor hashes. The 22 distinct
binary blobs use SHA-256 names. `constructor.c.txt` stores the exact original
unformatted C input as data; it is materialized as `fixture.c` for compilation. All corpus bytes, including metadata serialization,
are exact frozen inputs protected by scoped `-text`; fixtures remain read-only and
are copied into fresh scenario-owned work directories.

## Contracts and non-loading proof

Inspection uses the requested adapter spelling, while metadata validation invokes
`--validate` with the frozen module metadata. The host compares file and byte-buffer
validation results; its existing equality assertion is explicitly retained under
NDEBUG for Release builds (dependency documented in issue #380). No production
implementation changed.

The constructor case compiles the unchanged frozen C source using the configured
real compiler with `-shared -fPIC`; its exported descriptor and adapter are valid,
but loading the library would execute an aborting constructor. Successful static
inspection must not run it. This test runs on all supported platforms, including
Windows; no constructor skip or fake compiler substitutes for the original case.

Each inspection retains a 10-second subprocess deadline; compilation retains
30 seconds, within an outer 60-second scenario bound (CTest 90 seconds). Success
means normal status zero; parser rejection accepts any normal nonzero status,
as before. Signals/timeouts/cleanup failures are never intentional rejections.
Output contents remain unspecified. Both inspection streams are strictly decoded
as UTF-8 before accepting status, matching UTF-8 CI locales; this deterministic
malformed-output policy is stricter than a legacy non-UTF-8 Windows Python locale,
while the actual frozen cases' diagnostics are ASCII. Compiler output is not
text-decoded, matching the original inherited compiler streams. Standard context
logging and `--keep-work` preserve diagnostics and cleanup on failures.

## Original-to-C mapping

IDs have prefix `integration.native_artifact.` and retain original source order.

| ID suffix | Operation | Expected | Image bytes |
| --- | --- | --- | ---: |
| elf-valid | inspect | success | 4096 |
| elf-missing-adapter | inspect | reject | 4096 |
| elf-descriptor-not-function | inspect | reject | 4096 |
| elf-descriptor-not-object | inspect | reject | 4096 |
| elf-descriptor-hidden | inspect | reject | 4096 |
| elf-descriptor-undefined | inspect | reject | 4096 |
| elf-symbol-size-overflow | inspect | reject | 4096 |
| elf-envelope-size-overflow | inspect | reject | 4096 |
| elf-envelope-not-terminated | inspect | reject | 4096 |
| elf-adapter-not-executable | inspect | reject | 4096 |
| pe64-valid | inspect | success | 4096 |
| pe32-valid | inspect | success | 4096 |
| pe-missing-adapter | inspect | reject | 4096 |
| pe-forwarded-adapter | inspect | reject | 4096 |
| pe-descriptor-forwarder | inspect | reject | 4096 |
| pe-adapter-in-data | inspect | reject | 4096 |
| pe-ordinal-overflow | inspect | reject | 4096 |
| pe-envelope-size-overflow | inspect | reject | 4096 |
| not-image | inspect | reject | 12 |
| metadata-valid | validate | success | 4096 |
| metadata-main-hash | validate | reject | 4096 |
| metadata-minimum-os | validate | reject | 4096 |
| metadata-specialization-id | validate | reject | 4096 |
| metadata-wrong-adapter | validate | reject | 4096 |
| real-compiler-constructor | compile | success | real compiled library |

## Validation

Run `ctest -L native-artifact-migration --output-on-failure` in Debug, Release,
and UBSan, with the unchanged original script alongside it before deletion.
Remote parity covers Linux glibc/musl and Windows MINGW32/MINGW64/UCRT64.
Local results and independent controls are recorded below when complete.

Local Linux validation passed:

- Debug, Release and UBSan: 25/25 C IDs each, including real compilation.
- Unchanged Python: 25/25 in each of the same configurations.
- Independent fake-host/compiler controls: 22/22 across Debug and Release.
  They reject wrong success/rejection statuses, crashes masquerading as parser
  rejection, malformed UTF-8 in either stream, and failed constructor compilation.
  They deliberately accept an ordinary nonzero status of 23 for expected parser
  rejection and unspecified valid textual stdout/stderr, preserving original scope.
- An independent `dlopen` executable loading a library compiled from the frozen
  constructor source terminated with SIGABRT, whereas every static inspection
  passed. Thus the constructor remains live and the non-loading proof is meaningful.
- Corpus hashes and all frozen metadata bytes were verified; independent review
  matched all 24 synthetic cases, 29 associated payload snapshots, and constructor
  source against the original Python boundaries.
- clang-format 18 checks the runner and host; `git diff --check` passes.

The original Python remains intact pending full remote platform parity.

Explicit compiler paths containing `/` or `\` are resolved against the caller's
working directory before entering isolated scenario work; bare compiler names
retain PATH lookup. Focused Debug/Release controls both passed with a caller-local
`./cc-wrapper` and with bare `cc` (four positive checks). The complete 25-case
Debug/Release/UBSan suites and 22 independent controls passed again after this fix.
