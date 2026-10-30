# Loop and index scenario migration (#339)

`loop_index_testing` replaces `scripts/check_for.py` and `scripts/check_index.py`.
It uses `test/support/language_execution.*` and the shared scenario execution
framework; input fixtures remain read-only in `test/data/functional`.

| Old independent runner / checks | Stable CTest ID |
| --- | --- |
| `check_for.py`: all `for_*` fixtures, positive output and negative exact diagnostics, `none` and `all` | `native.for` |
| Same: regenerated source and persisted VM bytecode for every positive fixture | `native.for` |
| Same: compile (do not execute) an infinite loop | `native.for` |
| Same: persisted `native_modes/loops.goat`, source removed and CC unavailable; ready, 12 attempts/successes and zero retries | `native.for` |
| Same: `native_modes/long_loops.goat`, million iterations and shallow calls; 3 native attempts/successes, zero retries | `native.for` |
| `check_index.py`: `index_*`, `array_*`, `inheritance_*`, `loops_*`, `switch_*`, `compound_*`, `iteration_*`, `flat_*`, `conditional_*`, `finally_*`, `access_*` | `native.index` |
| Same: positive output, source round trip, saved VM after deleting source; `none` and `all`, compiler unavailable | `native.index` |
| Same: negative fixtures compiled at both optimization levels with exact status/stdout/stderr | `native.index` |
| Same: nine invalid access declarations in English and Russian | `native.index` |
| Same: three definite access violations × both languages × both optimization levels; declaration location, no `0:0`, no output artifact | `native.index` |

Build and run from the repository root:

```sh
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel 2
ctest --test-dir build/manual --output-on-failure -L language-roundtrip
build/manual/loop_index_testing native.for build/manual/goat build/manual test/data --keep-work
build/manual/loop_index_testing native.index build/manual/goat build/manual test/data
```

Windows uses the same commands with `.exe` executable suffixes. CTest supplies
absolute paths and the configured C compiler. These are supported on all current
Linux (glibc/musl, x86-64/AArch64) and Windows MinGW (i686/x86-64/UCRT) targets;
there are no platform skips. Existing CI's Linux and Windows steps now invoke the
same CTest IDs. Other sanitizer, fault-injection and packaging steps are unchanged.

Each invocation creates a unique directory beneath the build root. The shared
180-second scenario deadline covers child execution and its process-tree cleanup;
CTest allows 200 seconds. Required checks are active under `NDEBUG`. Output and
command diagnostics are retained on failure; `--keep-work` also retains successful
runs. No generated files are written into test/data.

## Differential verification

Before removing the Python runners, both were run with the same newly built Goat
and the unchanged `test/data/functional` tree. Python passed 30 loop fixtures plus
three integration checks and 95 index fixtures (including the post-summary access
checks). C runs are checked against those same exact input and expected files.
The removed scripts can be reproduced from parent revision `924f2ea9`:

```sh
git show 924f2ea9:scripts/check_for.py > scripts/check_for.py
git show 924f2ea9:scripts/check_index.py > scripts/check_index.py
python3 scripts/check_for.py build/manual/goat build/python-for
python3 scripts/check_index.py build/manual/goat build/python-index
# Remove only these restored obsolete runners after comparison.
```

To verify rejection without changing the originals, copy `test/data` to a separate
read-only fixture root outside the build directory, change one selected
`expected_output.txt` to an intentionally incorrect value, and run the relevant
C scenario against that root. It must return nonzero and preserve expected/actual
bytes in its diagnostic workspace. Both positive and negative parity checks must
also pass in Release.

Local verification also intentionally replaced `for_basic/expected_output.txt`
and `index_assignments/expected_output.txt` in a read-only copied data tree: both
Debug and Release rejected the wrong output and retained diagnostic files.
`native.index` passed Debug and Release. This executor lacks Linux
`/proc/thread-self/children`: when native compiler descendants need adoption, the
existing process supervisor correctly reports `cleanup_error=ENOENT`. Local
`native.for` therefore cannot establish a complete clean native run here; the
actual CI Linux/Windows/musl jobs provide that check, without disabling cleanup or
weakening any assertion.

# Characters, Unicode and interactive migration (#340)

`unicode_interactive_testing` replaces `scripts/check_characters.py`,
`scripts/check_unicode.py` and `scripts/check_interactive.py`. Its case bodies are
`characters_cases.c`, `unicode_cases.c` and `interactive_cases.c` in this directory.
The original Unicode hex fixture remains read-only in `test/data/unicode`.

| Old runner / checks (including parameter branches) | Stable CTest ID |
| --- | --- |
| `check_characters.py`: NUL, emoji, string concatenation, scalar ordinals and uint32 maximum; VM, regenerated source and bytecode with optimization `none`/`all` | `native.characters` |
| Same: 0, 65, 65535, 65536, 128512, 1114111, 2147483648, 4294967295 and -1; VM/native uint32 conversion; nine native successes and zero retries; persisted required-native bytecode | `native.characters` |
| Same: saved C and ABI host invocation, normal and `-fshort-wchar` builds | `native.characters` |
| `check_unicode.py`: Unicode directory/file names through source and bytecode; cleanup of Unicode paths | `native.unicode` |
| Same: independent portable UTF-32LE fixture equals frozen hex bytes; empty/ASCII/Cyrillic/emoji/combining/NUL strings; valid execution | `native.unicode` |
| Same: five legacy magic corruptions with recomputed checksum rejected | `native.unicode` |
| Same: lengths, equality, indexOf, uppercase/lowercase including contextual sigma and expansion, replace/format with embedded NULs; locales C/C.UTF-8/tr_TR.UTF-8; optimization none/all, VM bytecode, regenerated source and required-native report (at least two successes, zero retries) | `native.unicode` |
| Same: literal NUL inside string accepted, raw NUL outside string rejected, comment markers inside string retained | `native.unicode` |
| `check_interactive.py`: previous-line assignments/for loop, implicit/explicit constant immutability | `native.interactive` |
| Same: closure identity after exception, repeated exceptions/captured writes, nested closure collection, exception inside call preserving session | `native.interactive` |
| Same: syntax recovery, blank-line diagnostic numbering, continuation diagnostic/recovery, continuation comment boundaries, multiline function | `native.interactive` |
| Same: Unicode names/values, 100 accumulated declarations preserving function addresses, EOF without newline, empty input, explicit optimization none | `native.interactive` |
| Same: exact fresh-line prompts; prompt-filtered output and exact diagnostic line counts/ordered substrings | `native.interactive` |
| Same: 12 incompatible option combinations rejected before input execution, no stdout, required diagnostic and no output artifact | `native.interactive` |

Build and run (substitute `.exe` on Windows):

```sh
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel 2
ctest --test-dir build/manual --output-on-failure -L unicode-interactive
build/manual/unicode_interactive_testing native.characters build/manual/goat build/manual test/data --keep-work
build/manual/unicode_interactive_testing native.unicode build/manual/goat build/manual test/data
build/manual/unicode_interactive_testing native.interactive build/manual/goat build/manual test/data
```

All three scenarios use a unique work directory below the supplied build root,
read-only data, the common 180-second deadline and a 200-second CTest timeout.
Failures retain complete command/byte diagnostics and clean up owned processes;
`--keep-work` retains successful runs too. Checks do not depend on C `assert` and
remain active in Release. Linux x86-64/AArch64 and Windows MinGW i686/x86-64/UCRT
use the same scenarios; no platform branches are silently skipped. The native
compiler/short-wchar host checks remain real compilation and execution checks.

## Differential and negative verification (#340)

The three original Python runners passed before removal, using the same Goat
and inputs as the C migration. Interactive Python and C both passed all 30
checks against `build/339-debug/goat`. A temporary C copy with its first expected
interactive output changed to `wrong`, compiled with `-O2 -DNDEBUG`, failed with
exit 1 and retained expected/actual diagnostics. This proves the output check is
active independently of assertions or debug compilation.

Local native portions of the characters/Unicode C scenarios encounter the same
executor limitation described above: missing `/proc/thread-self/children`
produces `cleanup_error=ENOENT`. This is a failed local verification, not a SKIP
or a successful full parity result. No process cleanup or native requirement is
weakened; full native parity must be established by the applicable repository CI jobs.

The originals can be restored from the migration's parent for reproduction:

```sh
for name in characters unicode interactive; do
  git show cbfdf31625a76313807c93ebc55258616e30454a:scripts/check_${name}.py > scripts/check_${name}.py
done
python3 scripts/check_characters.py build/manual/goat build/python-characters
python3 scripts/check_unicode.py build/manual/goat build/python-unicode
python3 scripts/check_interactive.py build/manual/goat
# Remove only these restored obsolete runners after comparison.
```

The integrated runner builds in Debug and Release; `native.interactive` passes
in both. A Release run using a copied `unicode/scalars.gbin.hex` containing only
`00` fails at the independent fixture comparison (516 expected bytes versus 1),
retaining binary diagnostics. Character host compilation uses the configured
CMake C compiler directly, so every CI matrix compiler is exercised without
shell parsing. The two host modes retain normal and `-fshort-wchar` coverage.
A separate character mutant changes the first expected scalar output from `0`
to `9`; compiled with `-O3 -DNDEBUG` and Release support libraries, it fails with
exit 1 at the first 36-byte output comparison and retains diagnostics.

## Standalone bytecode v1 (#341)

`bytecode_v1_testing` replaces `scripts/check_bytecode_v1.py` without changing
`GOATBC01`, the standalone `GOATPG01` envelope, or the unresolved format decision
in #316. It links the independent test-only `goat_format_oracle`, never the
production decoder to compute expected answers.

| Original check | Stable CTest ID |
|---|---|
| Independent scalar/function producer equals frozen `binary-v1/scalars.hex`; exact int64 boundaries, negative zero, infinity, NaN, embedded NUL and Unicode output | `native.bytecode_v1` |
| Frozen empty bytecode executes without output | `native.bytecode_v1` |
| Six legacy/wrong magic values, all 14 header fields, six truncations and trailing byte | `native.bytecode_v1` |
| Invalid opcode, reserved instruction fields, ARG-consumer jumps, invalid/aliased descriptors, invalid Unicode scalars/terminator and self-referencing parameter vector | `native.bytecode_v1` |
| File/function/location fields, mixed synthetic/real positions, wrong function name/creation/body/ownership IDs and entry inside function; 63 corruptions total | `native.bytecode_v1` |
| Every rejection requires nonzero status, English binary diagnostic, no user stdout, and unchanged input bytes | `native.bytecode_v1` |
| Full-width sparse function ID `UINT32_MAX` retains valid execution/output | `native.bytecode_v1` |
| Independent reference validates scalar image; empty C-writer bytecode matches independent empty image with synthetic positions | `native.bytecode_v1` |
| Nested and unused functions compile under optimize `none` and `all`; envelope magic/flags and independent code validation; extracted code executes as `42` | `native.bytecode_v1` |

`test/data/binary-v1/empty.bytecode.hex` is a byte-for-byte copy of the existing
`docs/module-format-v1/vectors/empty.bytecode.hex`, not a regenerated expectation.
Both frozen images are read-only during tests. All platforms execute the same
checks: Linux x86-64/AArch64, Windows MinGW i686/x86-64/UCRT, Debug/Release and
musl. No platform-specific skips are introduced. The scenario uses a unique
work directory, a shared 180-second deadline (CTest 200 seconds), fail-fast
checks active under `NDEBUG`, complete command/output diagnostics on failure,
and process/resource cleanup. `--keep-work` preserves successful diagnostics.

```sh
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel 2 --target bytecode_v1_testing
ctest --test-dir build/manual --output-on-failure -R '^native.bytecode_v1$'
build/manual/bytecode_v1_testing native.bytecode_v1 build/manual/goat build/manual test/data --keep-work
# Explicit replacement for Python --generate; only the supplied output is written:
build/manual/bytecode_v1_testing --generate build/manual/scalars.hex
cmp build/manual/scalars.hex test/data/binary-v1/scalars.hex
```

For differential reproduction, restore only the original runner from the parent:

```sh
git show 735ded1b8211de0b42a7ef9e317488e90fbeeb66:scripts/check_bytecode_v1.py > scripts/check_bytecode_v1.py
python3 scripts/check_bytecode_v1.py build/manual/goat build/manual/python-bytecode
# Remove the restored obsolete runner after comparison.
```

Before removal, the Python runner passed against both `build/339-debug/goat`
and `build/339-release/goat`, including all 63 corruptions and both optimization
modes. The C runner's differential and negative verification is recorded below.

The C runner passed the same frozen inputs and Goat binaries in Debug and
Release, including all 63 rejections and both optimization modes. Direct
`--keep-work` retained the complete successful run, and `--generate` reproduced
`scalars.hex` byte for byte. A temporary C mutant changed the first expected
int64 output digit, was compiled with `-O3 -DNDEBUG` and Release support libraries,
and failed with exit 1 at the output comparison. A second Release run against
a copied data directory whose scalar fixture contained only `00` failed with
exit 1 at the independent fixture comparison. Both retained diagnostics. These
negative checks demonstrate that Release does not compile away the assertions.
The migrated source also passes `gcc -std=c11 -Wall -Wextra -Werror` syntax checks.

## Metadata JSON (#342)

`module_json_scenarios` replaces `scripts/check_module_json.py`. The production
host remains `module_json_testing`; no production parser/validator is used to
calculate expected results. The read-only corpus has 440 original requests,
93 independently normalized expected values and 347 required rejections.
See [provenance](../../data/module-json/README.md).

| Original check / original source lines | Stable CTest ID |
|---|---|
| Summary consistency in both orders: overlap/disjointness, arity 0/1/2, exact int64 and >2^53, Boolean, Unicode/NUL, real bit patterns, ranges, arrays, normal return and effects (65–114) | `native.module_json` |
| Gmod defaults, absent/null required fields, optional metadata emptiness, Unicode/NUL and author validation (119–147) | `native.module_json` |
| Integer lexemes/bounds, unsupported versions, BOM, trailing input, comments, invalid UTF-8/surrogates and duplicate escaped keys (148–161) | `native.module_json` |
| Exact untrimmed main path, including POSIX real-file lookup; Windows keeps original omission of only that file-system branch (162–173) | `native.module_json` |
| Native options and dependency shapes, identities, hashes and duplicate versions (174–191) | `native.module_json` |
| uint32 entry/main boundaries, negative zero versus exponent, bytecode version (192–202) | `native.module_json` |
| Rich nested descriptor and every closed record's unknown/NUL-suffixed keys; open maps retain schema-checked entries (203–218) | `native.module_json` |
| int64 summary bounds, nested target/specialization/function/effect corruption, duplicates and nesting limit (219–238) | `native.module_json` |
| Four original module vectors, every identity position, 255/250-byte boundaries and distinct version strings (239–258) | `native.module_json` |

The C scenario uses a unique build work directory, a cumulative 180-second
deadline (CTest 200), scenario-owned processes/resources and retained diagnostics
on failure. `--keep-work` preserves successful work. Checks remain active under
`NDEBUG`. Debug and Release CI plus the nonzero automatic-variable initialization
job use the C runner. Linux x86-64/AArch64 and MinGW i686/x86-64/UCRT use the same
corpus; no structural error is converted to a successful parse or skipped.

```sh
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel 2 --target module_json_scenarios
ctest --test-dir build/manual --output-on-failure -R '^native.module_json$'
build/manual/module_json_scenarios native.module_json build/manual/module_json_testing build/manual test/data --keep-work
# Differential reproduction before deleting the restored original:
git show 5e56f72c57c5df34237dd330e9bd420d4d2131e7:scripts/check_module_json.py > scripts/check_module_json.py
python3 scripts/check_module_json.py build/manual/module_json_testing
```

Verification before removal: the unchanged Python runner and new C runner both
passed all 440 requests against Debug and Release hosts. Every positive fixture
also matched the independent Python reference. Direct Release `--keep-work`
passed and retained artifacts. Two copied-corpus Release runs were deliberately
corrupted: one changed an expected module name, the other replaced an accepted
input with `{`. Both exited 1 and retained diagnostics, proving that bad expected
results and structural errors are detected with `NDEBUG`. The source passes
`gcc -std=c11 -Wall -Wextra -Werror` syntax checks.

## Canonical JSON and main hashes (#343)

`module_canonical_hash_scenarios` replaces both Python runners, retaining all 935
original process requests. Each successful result is compared both to frozen
Python expectations and to the independent C format oracle (never production
normalization/hash code). See [corpus provenance](../../data/module-canonical-hash/README.md).

| Original checks | Stable CTest ID |
|---|---|
| `check_module_canonical.py`: exact bytes, control/Unicode/NUL keys, int64 limits and >2^53, invalid JSON, idempotence | `native.module_canonical` |
| Scalar facts, real bit patterns, ranges, reductions, duplicate/invalid summaries, argument arities, effects permutations | `native.module_canonical` |
| Function permutations, external validation, nine-field projection, vectors, gmod defaults, producer/native ordering and frozen key permutations | `native.module_canonical` |
| `check_module_hash.py`: four vectors, all 145 SHA block/padding lengths, success/mismatch verification | `native.module_hash` |
| Formatting/defaults/information invariance, significant metadata/byte mutations, truncation/extension, dependency permutations/hashes | `native.module_hash` |
| Native exclusion, fact normalization/significance, invalid metadata and duplicate keys | `native.module_hash` |

All cases apply on Linux x86-64/AArch64 and Windows MinGW i686/x86-64/UCRT.
The runner uses scenario-owned processes/resources, unique build directories,
a cumulative deadline and retained failure diagnostics. `--keep-work` retains
successful artifacts; checks remain active in Release. CI integration and
Release selection include both IDs, and stack-pattern initialization runs both.

```sh
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel 2 --target module_canonical_hash_scenarios
ctest --test-dir build/manual --output-on-failure -R '^native.module_(canonical|hash)$'
build/manual/module_canonical_hash_scenarios native.module_hash build/manual/module_json_testing build/manual test/data docs/module-format-v1/schema-source.json --keep-work
# Recover old runners for differential reproduction, then remove restored files:
git show 09302829bd05aec9d47a51415e7cdb42d865eea5:scripts/check_module_canonical.py > scripts/check_module_canonical.py
git show 09302829bd05aec9d47a51415e7cdb42d865eea5:scripts/check_module_hash.py > scripts/check_module_hash.py
python3 scripts/check_module_canonical.py build/manual/module_json_testing
python3 scripts/check_module_hash.py build/manual/module_json_testing
```

Verification before removal: both unchanged Python suites passed against Debug
and Release hosts (422 canonical + 513 hash requests). The C scenarios passed
the same corpus in both configurations, including the independent C comparisons.
A direct Release hash run with `--keep-work` passed and retained its work.
Two copied-corpus Release runs intentionally replaced the first expected result
and the first accepted input with `{`; each failed with exit 1 and retained
failure diagnostics. These prove mismatches and malformed input cannot pass
with `NDEBUG`. Formatting was checked with clang-format 18.

## ZIP wrapper (#344)

`zip_scenarios` replaces `scripts/check_zip.py`; `zip_testing` remains the
production subprocess host. Inputs are constructed with the independent
`test/support/format_zip.c` builder and bounded byte mutations, never the
production writer. Production writer results are extracted with the independent
reader as well as the production reader. The historical frozen payload/damage
recipes under `test/data/python-samples` are unchanged.

| Original check | Stable CTest ID |
|---|---|
| Direct API invalid arguments, empty archive, capacity canaries | `integration.zip` |
| Stored/Deflate/mixed archives; signed data descriptors; duplicate, binary, Unicode, empty and 60,000-byte names; comments/extras; exact-capacity reads | `integration.zip` |
| Embedded NUL names, every truncation, malformed extra lengths and ZIP64 extra ID | `integration.zip` |
| Nine local/central inconsistencies, CRC field and payload corruption | `integration.zip` |
| Encryption flags, unsupported compression, symlink metadata, ZIP64 sentinels and multi-disk fields | `integration.zip` |
| SFX prefix/trailer and unsafe/directory path spellings preserved by wrapper | `integration.zip` |
| Invalid Deflate, four uncompressed sizes, three compressed sizes and inserted stream junk | `integration.zip` |
| Both writer methods over seven payloads including frozen 65,537 bytes; determinism, binary comments, separate local/central extras, timestamps/flags/attrs, independent extraction | `integration.zip` |
| Exact writer limit, limit-minus-one and six small limits; duplicate/empty archive; invalid names, method and extras | `integration.zip` |
| Local-only forced ZIP64, exact frozen profile-1 `empty.gbin`, malformed lengths/offsets | `integration.zip` |
| All 60 frozen three-XOR damage recipes, original Linux/Windows base selected explicitly, bounded status set | `integration.zip` |

The ID is preserved from the previous standalone Python suite. All assertions
remain active in Release, including the subprocess host's API assertions.
Each invocation owns a unique build workspace, has one cumulative 180-second
deadline (CTest 200), retains failure diagnostics, releases processes/resources,
and supports `--keep-work`. Inputs remain read-only. The same scenario runs on
Linux x86-64/AArch64, Windows MinGW i686/x86-64/UCRT and musl; the frozen damage
base is the only platform-dependent input, with no new skip paths.

```sh
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel 2 --target zip_scenarios
ctest --test-dir build/manual --output-on-failure -R '^integration.zip$'
build/manual/zip_scenarios integration.zip build/manual/zip_testing build/manual test/data docs/module-format-v1/vectors --keep-work
# Restore the old runner for differential reproduction, then remove it:
git show 2ceda9039f23d29d5c207247731c90cefd09086e:scripts/check_zip.py > scripts/check_zip.py
python3 scripts/check_zip.py build/manual/zip_testing
```

Verification before removal: unchanged Python passed 333 subprocess requests in
both Debug and Release; C passed 329 including the direct host API invocation.
The difference is five redundant production `inspect` calls used only to obtain
mutation offsets: C obtains those offsets from the independent reader instead.
The actual metadata assertions and every truncation remain. Direct Release
`--keep-work` retained successful diagnostics. Two copied-input Release runs
intentionally flipped a byte in the expected `empty.gbin` and replaced the frozen
damage recipe with `{}`; both failed with exit 1 and retained diagnostic files.
The new source passes `gcc -std=c11 -Wall -Wextra -Werror` syntax checks and
clang-format 18.

A temporary request capture additionally compared every non-API C request with
the original Python run: all 328 matched exactly for ZIP input bytes, allowed
statuses and required stdout. Writer JSON matched after normalizing object key
order and omitted versus explicit empty optional fields. This includes the
hand-built local-only ZIP64 archive and the frozen damage requests; no generated
expectation corpus or temporary capture helper is retained in the repository.

## Module container (#345)

`module_container_scenarios` replaces `scripts/check_module_container.py`;
`module_container_testing` remains the production subprocess host. The C runner
builds ZIPs and hashes with the independent `test/support/format_zip.c` and
format oracle, not production container/writer/hash routines. Frozen damage
inputs under `test/data/python-samples` remain unchanged and read-only.

| Original check | Stable CTest ID |
|---|---|
| Direct API invalid arguments, null accessors and snapshot ownership after overwriting/freeing the ZIP | `integration.module_container` |
| Golden empty container; Stored/Deflate, signed/unsigned descriptors, three extras, binary comments, mixed order/methods and optional native directory | `integration.module_container` |
| Five bytecode lengths including empty and 1,024 bytes, independent main hashes | `integration.module_container` |
| Both foreign native variants, independent SHA-256; corrupt payload/hash and missing/undeclared libraries regardless of host | `integration.module_container` |
| Exact expanded limit, minus-one/zero, sum overflow and huge declared entry before allocation | `integration.module_container` |
| Empty/missing mandatory parts, unknown/duplicate JSON keys, wrong main hash and altered bytecode | `integration.module_container` |
| Seventeen forbidden paths including embedded NUL/backslash/Unicode; five case-fold collisions, duplicate and nonempty native directory | `integration.module_container` |
| Unix special-file kinds, DOS directory/reparse/device/volume flags, mismatched directory attrs and six unsafe extras | `integration.module_container` |
| Nine local field mutations in both streaming modes, five flags, unsupported method/version, three descriptor fields | `integration.module_container` |
| Native/directory CRC errors, invalid Deflate, four advertised sizes and bounded huge-size rejection | `integration.module_container` |
| Prefix/suffix, alias offset, overlapping/short physical spans and absent/corrupt descriptor | `integration.module_container` |
| ZIP64/multidisk sentinels, local-only forced ZIP64 and every truncated prefix | `integration.module_container` |
| All 100 frozen three-XOR damage recipes, bounded status set and no partial output | `integration.module_container` |

The previous standalone scenario ID is preserved; related inner assertions stay
together. The common framework supplies a unique workspace under the build,
a cumulative 180-second deadline (CTest 200), command/stdout/stderr diagnostics,
resource cleanup, failure retention and `--keep-work`. Checks remain active in
Release, including API assertions in the subprocess host. The suite has no
host-specific skip paths: Linux x86-64/AArch64, MinGW i686/x86-64/UCRT and musl
use the same cases. Debug build scripts and Release CI explicitly build the
runner; Release CI selects the `container-migration` label.

```sh
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel 2 --target module_container_scenarios
ctest --test-dir build/manual --output-on-failure -R '^integration.module_container$'
build/manual/module_container_scenarios integration.module_container build/manual/module_container_testing build/manual test/data docs/module-format-v1/vectors --keep-work
# Differential reproduction (restore only temporarily):
git show 8d5479ba6499e7909c1b4e39582d37657fe67cb2:scripts/check_module_container.py > scripts/check_module_container.py
python3 scripts/check_module_container.py build/manual/module_container_testing
```

Verification before removal: unchanged Python and C each passed all 714
validations in Debug and Release. Temporary request capture matched 701 C
requests byte-for-byte (including all 470 truncations, 100 frozen mutations and
local-only ZIP64). The remaining 13 native fixtures contain identical expanded
bytes and metadata; independent zlib level 9 versus Python's default level 6
changes only compressed sizes/derived offsets. Replaying those C inputs through
the original Python `check()` preserved accepted statuses, limits, output and ZIP
diagnostics. No temporary capture/replay helper or generated corpus is shipped.

Release `--keep-work` retained successful diagnostics. A copied frozen-recipe
file replaced by `{}` and a flipped expected bytecode byte both failed with
exit 1 and retained diagnostics. Related Debug format/ZIP tests passed 50/50;
clang-format 18 and strict C11 `-Wall -Wextra -Werror` syntax checks passed.
A fresh UBSan build with Python discovery disabled also passed the scenario.

## Module bytecode (#346)

`module_bytecode_scenarios` replaces `scripts/check_module_bytecode.py`;
`module_bytecode_testing` remains the production subprocess host. The runner
constructs independent wire images and metadata in C, using the test-only
external-declaration fixture helper. No production writer, validator or compiler
is used to calculate expected inputs or results.

| Original check | Stable CTest ID |
|---|---|
| Direct API invalid arguments, null outputs, snapshot ownership and allocation balance | `integration.module_bytecode` |
| Golden empty/two-version vectors, empty function table and container-owned main hash | `integration.module_bytecode` |
| Logical main file versus relocated instruction entry, nonexistent/repeated filenames, FILE references | `integration.module_bytecode` |
| Invalid/missing file metadata, seven main values, five entry mismatches and table counts | `integration.module_bytecode` |
| Two retained functions, legal ID gap, maximum uint32 ID, reordered metadata, function and parameter mismatches | `integration.module_bytecode` |
| Embedded NUL/supplementary Unicode parameter names and exact-length comparison | `integration.module_bytecode` |
| Own export IDs, fabricated entry mismatch, deferred foreign reference and native specialization IDs | `integration.module_bytecode` |
| IMPORT after END, duplicate uses, two dependency versions, eleven malformed imports and removed/unused imports | `integration.module_bytecode` |
| Assignment preflight descriptor kinds/bounds, maximum count, CHECKBOUND declaration membership and reserved operands | `integration.module_bytecode` |
| File-scope external cells, names/mutability/modifiers, abrupt transfer, nested blocks, post-END padding, duplicates and function-body declarations | `integration.module_bytecode` |
| Reserved launch names in lexical declarations versus legal strings/properties, exact NUL/prefix distinctions | `integration.module_bytecode` |
| Invalid opcodes/references/operands, empty/truncated/trailing bytecode and exact JSON integer syntax | `integration.module_bytecode` |

The original standalone scenario ID is preserved. Its shared scenario framework
provides one cumulative 180-second deadline (CTest 200), a unique build workspace,
command/stdout/stderr diagnostics, cleanup, failure retention and `--keep-work`.
Input vectors remain read-only. Checks, including host API assertions, remain
active in Release. There are no platform-specific skip paths: the same suite
runs on Linux x86-64/AArch64, MinGW i686/x86-64/UCRT and musl. Debug scripts and
Release CI explicitly build the runner; Release selects `module-bytecode-migration`.
The shared Python external-declaration helper remains for other suites listed in
`test/data/binary-oracle/README.md`, pending #394; this runner exports no helpers.

```sh
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel 2 --target module_bytecode_scenarios
ctest --test-dir build/manual --output-on-failure -R '^integration.module_bytecode$'
build/manual/module_bytecode_scenarios integration.module_bytecode build/manual/module_bytecode_testing build/manual test/data docs/module-format-v1/vectors --keep-work
# Differential reproduction (restore only temporarily):
git show b30c7be31f4cea93377d4682e82dbada81803cf4:scripts/check_module_bytecode.py > scripts/check_module_bytecode.py
python3 scripts/check_module_bytecode.py build/manual/module_bytecode_testing
```

Before removal, unchanged Python and C each passed all 182 validations plus the
host API invocation in Debug and Release. Temporary subprocess request capture
matched all 182 images byte-for-byte and all metadata after JSON whitespace/key
order normalization, with identical expected statuses and entry outputs. No
capture helper or generated expectation corpus is shipped. Twelve related
format-binary/metadata/hash/container CTests passed. A fresh UBSan build with
Python discovery disabled passed the migrated scenario. Release `--keep-work`
retained successful diagnostics; copied vectors with a flipped bytecode magic
byte or `{}` metadata both failed with exit 1 and retained diagnostic files.
Clang-format 18 and strict C11 `-Wall -Wextra -Werror` syntax checks passed.

## Module ABI and native selection (#347)

`module_abi_scenarios` and `module_native_scenarios` replace
`scripts/check_module_abi.py` and `scripts/check_module_native.py`. The existing
component hosts remain the production subprocesses. Fixtures use the independent
format oracle for descriptor JSON, guard intersections, hashes and ZIP assembly;
no production selector/guard/writer computes an expected result.

| Original check | Stable CTest ID |
|---|---|
| Direct API layout, scalar values, services and one-shot invocation | `integration.module_abi` |
| Canonical descriptor envelope, nine wire damages, six malformed/noncanonical JSON inputs | `integration.module_abi` |
| ABI versions, required/unknown fields, invalid specialization IDs/signatures/guards, maximum IDs and overlap precedence | `integration.module_abi` |
| All six scalar types, missing/extra/wrong-type arguments, equality/inequality, int64 limits, NaN/infinities/signed zero/subnormal, NUL/Unicode strings and characters | `integration.module_abi` |
| All 600 frozen guard pairs, independently recalculated intersections | `integration.module_abi` |
| Real baseline shared library compiled against the public ABI header, descriptor export and services-backed result 42 | `integration.module_abi` |
| Direct selector API, exact ABI membership including empty/duplicate/maximum version lists | `integration.module_native` |
| Every individual and combined compatibility reason, missing CPU bit masks and unknown host knowledge | `integration.module_native` |
| Six OS/architecture/runtime profiles, all cross-profile pairs, 32/64-bit mismatch | `integration.module_native` |
| Numeric OS/runtime tuples, zero extension, uint32 limits, Windows build floors | `integration.module_native` |
| Inherent CPU baselines, unknown/NUL/Unicode features, impossible architecture features, AVX/SSE/SVE prerequisite chains | `integration.module_native` |
| OS library suffix, invalid foreign variant precedence, order-independent ambiguity and unique selection | `integration.module_native` |
| Payload/hash corruption of compatible and incompatible variants before filtering | `integration.module_native` |
| Compiler information retained but excluded from main hash/selection; malformed compiler metadata | `integration.module_native` |
| Strict profiles: missing fields, duplicate keys, closed objects, wrong types/versions/runtime | `integration.module_native` |
| Host facts unaffected by MSYSTEM/PATH changes; process architecture/runtime, compiler macro baselines | `integration.module_native` |
| Detected-host selection and real baseline shared-library call returning 42 | `integration.module_native` |

Both original standalone CTest IDs are preserved. The common scenario framework
provides unique build workspaces, cumulative deadlines, command/stdout/stderr
artifacts, cleanup, failure retention and `--keep-work`. Input vectors and the
frozen guard pairs remain read-only. Checks and component-host assertions remain
active in Release. The same suites apply to Linux x86-64/AArch64 and Windows
MinGW i686/x86-64/UCRT; no platform is silently skipped. The unchanged dispatch,
loading and native-mode suites retain `off`/`auto`/`required` coverage; those
modes were not implemented by either replaced Python script.

Debug build scripts and Release CI explicitly build both runners. Release CI
selects the `module-abi-native-migration` label. No imported helpers are exported
by these two Python scripts, so neither needs a compatibility shim.

```sh
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel 2 --target module_abi_scenarios module_native_scenarios
ctest --test-dir build/manual --output-on-failure -L module-abi-native-migration
build/manual/module_abi_scenarios integration.module_abi build/manual/module_abi_testing build/manual test/data docs/module-format-v1/vectors --keep-work
build/manual/module_native_scenarios integration.module_native build/manual/module_native_testing build/manual test/data docs/module-format-v1/vectors --keep-work
# Differential reproduction (restore only temporarily):
git show a6e02fe8595ec09696d27e97800ae5ef1b92bd96:scripts/check_module_abi.py > scripts/check_module_abi.py
git show a6e02fe8595ec09696d27e97800ae5ef1b92bd96:scripts/check_module_native.py > scripts/check_module_native.py
python3 scripts/check_module_abi.py build/manual/module_abi_testing
python3 scripts/check_module_native.py build/manual/module_native_testing
```

Before removal, unchanged Python and C passed the same Debug and Release hosts:
716 ABI checks (including 600 guard pairs) and 209 native-selection checks, plus
both API self-tests and real shared-library calls. Temporary request capture
matched all 115 ABI subprocess inputs before the platform-built library load
byte-for-byte for descriptor envelopes, and after JSON key/whitespace
normalization for scalar/overlap metadata. Native requests matched their profile,
ABI list, expanded archive entries and metadata after JSON formatting
normalization; ZIP compression and freshly compiled library bytes are not used
as expected outputs. All captured semantic outputs matched. No capture tool or
generated expectation corpus is shipped.

Fresh UBSan builds with Python discovery disabled passed both scenarios. Release
`--keep-work` retained successful diagnostics. Replacing a copied frozen guard
file by `{}` and flipping a copied bytecode magic byte each failed with exit 1
and retained diagnostics. Empty/malformed host output also fails rather than
being interpreted as a zero status. Related metadata/hash/container/bytecode and
format-oracle tests passed 38/38. Clang-format 18 and strict C11
`-Wall -Wextra -Werror` syntax checks passed.

## Source snapshots and file identity (#348)

`module_sources_scenarios` and `module_files_scenarios` replace
`scripts/check_module_sources.py` and `scripts/check_module_files.py`. They retain
`module_sources_testing` and `module_files_testing` as the production API hosts.
No language, module format, or production source code is changed.

| Old scenario/check | Stable CTest ID |
| --- | --- |
| Source host API self-test; unused sources/dependencies; descriptor defaults and omitted empty informational fields | `integration.module_sources` |
| DFS order/targets, conditional/function includes, cycles, canonical aliases and cwd independence with a poisoned module search path | `integration.module_sources` |
| External source identities, equal basenames/bytes, project-prefix boundary, absolute/external main and distinct snapshots | `integration.module_sources` |
| Literal extensionless/custom-extension main, environment/tilde literals, Unicode/emoji, POSIX spaces/backslashes and Windows separator/root/drive cases | `integration.module_sources` |
| Missing/empty/NUL main, required descriptor fields, invalid native/dependency metadata, duplicate keys and malformed UTF-8 | `integration.module_sources` |
| Comments/string/member decoys; invalid include expressions and malformed source; exact missing-source row/column | `integration.module_sources` |
| Directory/FIFO/device rejection, descriptor/source symlink bases and deduplication, external symlink and hard-link include bases | `integration.module_sources` |
| File/source/total limits, 1800-file cyclic DFS, snapshot does not execute source | `integration.module_sources` |
| File host API self-test; independently encoded 1/2/3/20-file images, 0/2 padding and every main file (52 valid cases) | `integration.module_files` |
| Invalid main type/range, cross-file/out-of-range/interior entry, malformed file metadata (17 cases) | `integration.module_files` |
| Duplicate external names and internal/external overlap, ASCII and Unicode identities (6 cases) | `integration.module_files` |
| Truncated/zeroed image and invalid metadata (3 cases) | `integration.module_files` |

Both runners use scenario-owned unique directories under the supplied build
root, a common deadline, captured command diagnostics, deferred resource cleanup,
and `--keep-work`. Success removes the work directory unless requested otherwise;
failure preserves it. Read-only inputs are never modified. The file runner reads
`docs/module-format-v1/vectors/empty.module.json` and independently writes the
wire image, rather than calling a production bytecode writer for expectations.
POSIX FIFO/device/literal-path checks are platform-specific; Windows equivalents
check separator normalization and ambiguous drive/root paths. Windows symlinks
may skip only for the explicit missing-privilege error; other creation errors
fail the scenario. Hard links are required on both platforms.

Reproduce (Debug or Release):

```sh
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel 2 --target module_sources_scenarios module_files_scenarios
ctest --test-dir build/manual --output-on-failure -L module-sources-files-migration
build/manual/module_sources_scenarios integration.module_sources build/manual/module_sources_testing build/manual test/data --keep-work
build/manual/module_files_scenarios integration.module_files build/manual/module_files_testing build/manual test/data docs/module-format-v1/vectors --keep-work
# Historical Python comparison, before removing the restored runners:
git show 8558112d21b0f1e6e1db21261dfd798de92d86b0:scripts/check_module_sources.py > scripts/check_module_sources.py
git show 8558112d21b0f1e6e1db21261dfd798de92d86b0:scripts/check_module_files.py > scripts/check_module_files.py
python3 scripts/check_module_sources.py build/manual/module_sources_testing
python3 scripts/check_module_files.py build/manual/module_files_testing
```

The CTest IDs remain in full Debug integration; explicit Linux and MinGW Debug
build lists and Release migration labels include both new runners. Alpine/musl
runs them in Debug and Release as well. CI links and final-commit verification
are recorded in the PR/issue, without an extra checklist-only push.

Local verification for this migration: Debug and Release each pass the original
Python and C suites (67 source fixtures on Linux and 78 file fixtures). Capturing
the old/new file-host requests gives identical bytecode bytes and semantically
identical JSON metadata for all 78 cases. The two C scenarios also pass with the
hosts and libraries instrumented by UBSan. Fake hosts returning an empty source
graph or wrong file-entry output are rejected in Release, preserving diagnostics.
Host assertions deliberately remain enabled in optimized builds: these legacy
hosts perform fixture setup and VM execution inside assertions, so disabling
them would remove checks (and break file-fixture execution).

## Static imports and source includes (#349)

`module_imports_scenarios` and `source_include_scenarios` replace
`scripts/check_module_imports.py` and `scripts/check_source_include.py`.
They retain the existing `module_imports_testing` and `source_include_testing`
production API hosts. No production implementation, language semantics, or
module format is changed. Neither removed script exports helpers to remaining
Python consumers.

| Old scenario/check | Stable CTest ID |
| --- | --- |
| Import API self-test, empty graph, descriptor defaults, unused defaults, explicit/implicit versions, missing defaults, source vs descriptor mode | `integration.module_imports` |
| Static collection without execution, comments/member/string decoys, Unicode locations, escapes, all order permutations and exact version strings | `integration.module_imports` |
| Include graph order, cycles/deduplication/external files, shared sites, missing import location and cwd independence with poisoned module path | `integration.module_imports` |
| Custom/default import limits (65,536/65,537), 16,384 dependencies, invalid syntax/specifiers/UTF-8/tokens/brackets/defaults | `integration.module_imports` |

Each source-include row below has the prefix `integration.source_include.`:

| Old source-include case/check | CTest ID suffix |
| --- | --- |
| Nested DFS/load order and saved file ordering | `nested` |
| Alias caching, shared mutation and function mapping, descriptor compilation | `repeat` |
| Lexical isolation and function source membership | `lexical` |
| Private/public access | `private` |
| Lazy/unexecuted inclusion and cached execution | `lazy` |
| Include expression values and argument boundaries | `expression`, `arguments` |
| Closure boundaries, callback failure/recovery | `closure-include`, `failed-callback` |
| Equal bytes remain different identities | `same-content` |
| Ready/early cycle access | `cycle-ready`, `cycle-early-caught` |
| Cached exception identity, finally, throwing included function | `failed-repeated`, `finally`, `throwing-function` |
| Unicode filenames/content and empty included file | `unicode`, `empty` |
| External equal basenames, descriptor compilation and path redaction | `outside` |
| Ten invalid literals/missing paths and invalid unexecuted child before any effects | `invalid` |
| Hard-link path identities and relative include bases | `hardlinks` |
| Symlink canonical source bases, deduplication and saved image | `symlinks` |
| Nested/anonymous/160 named function labels, source locations and byte-identical relocation | `diagnostic-mapping` |
| 100-deep chain, file count and saved execution after source removal | `deep` |

Every archived case independently checks ZIP entries, metadata/bytecode file and
function mappings, entry ranges, path redaction, then executes the saved image
after deleting its source directory (the diagnostic-mapping case instead compares
relocated archives byte-for-byte).

Both runners use the shared scenario framework: unique work directories under
the build root, one scenario deadline, captured command/stdout/stderr diagnostics,
deferred resource cleanup and `--keep-work`. Successful runs clean up; failures
retain their diagnostics. Test inputs are generated in those work directories,
not written into `test/data`. Assertions in the legacy component hosts remain
enabled in Release because they execute fixture setup and production API checks.

The `imports-include-migration` label runs the new C tests without Python.
Full Debug integration, explicit Linux and MinGW build targets, Release CI
selection and Alpine/musl Debug and Release all include the new runners.
Windows symlink creation can skip only the missing-privilege error; hard-link
checks remain required.

Reproduce (also repeat with `-DCMAKE_BUILD_TYPE=Release`):

```sh
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel 2 --target module_imports_scenarios source_include_scenarios
ctest --test-dir build/manual --output-on-failure -L imports-include-migration
build/manual/module_imports_scenarios integration.module_imports build/manual/module_imports_testing build/manual test/data --keep-work
build/manual/source_include_scenarios --list
# Historical differential runs (remove the restored scripts afterwards):
git show c63e677637ee04cc79390aa65152a0c834298586:scripts/check_module_imports.py > scripts/check_module_imports.py
git show c63e677637ee04cc79390aa65152a0c834298586:scripts/check_source_include.py > scripts/check_source_include.py
python3 scripts/check_module_imports.py build/manual/module_imports_testing
python3 scripts/check_source_include.py build/manual/source_include_testing
```

CI links and exact final-commit verification are recorded in the PR/issue,
without an extra checklist-only push.

Local verification: original Python and new C pass on the same Debug and Release
hosts (63 import fixtures plus the API self-test; 69 include invocations).
Temporary differential capture matched all 64 import subprocess requests and
results, including descriptor JSON, transitive source bytes, limits and outputs.
All 69 include requests match source bytes, descriptor JSON, output/status,
diagnostics (normalizing only workspace directory prefixes) and compiled archive
SHA-256. Independent scenario ordering was compared as a multiset, with no
mismatches. No capture tool or generated expectation corpus is shipped.

The 24 CTest scenarios pass in Debug, Release and a fresh UBSan build with Python
discovery disabled. Deliberately wrong output and a corrupted compiled archive
are rejected in Release with command diagnostics retained. Release
`--keep-work` retains successful fixtures. Clang-format 18 and strict C11
`-Wall -Wextra -Werror` syntax checks pass.

## Ordered module search (#350)

`module_search_scenarios` replaces `scripts/check_module_search.py`, including
its nested Python permission probe. The existing `module_search_testing` API
host retains its Python-compatible invocation for historical comparison and
adds test-only C fixture controls for retargeting and unprivileged probing.
No production search, language or format semantics are changed; the removed
runner has no remaining Python consumers.

| Old scenario/check | Stable CTest ID |
| --- | --- |
| API cleanup, descriptor/source/compiled roots, local/CLI/environment precedence, exact successful hash and ordered attempts | `integration.module_search` |
| Four successive fallthroughs, total absence, no recursive/version/library-directory discovery | `integration.module_search` |
| Invalid ZIP, wrong name/version/hash stop at first candidate; distinct diagnostics, invalid hash/name/version arguments | `integration.module_search` |
| Directory/FIFO candidates, empty segments, alias deduplication, relative Unicode CLI/environment paths, cwd and environment snapshots | `integration.module_search` |
| Absent/empty/separator-only process environment and explicit/absent modes; literal separators/home/variable syntax | `integration.module_search` |
| POSIX missing/file parent semantics; Windows incomplete drive/root rejection; unused missing directories and absent root | `integration.module_search` |
| Symlink root canonicalization for descriptor/compiled/source roots, retarget after preparation, directory aliases and dangling candidate/parent | `integration.module_search` |
| Native access-denied file/directory probes; valid first hit ignores inaccessible fallback, denied first candidate blocks valid fallback, diagnostic operation/path/native code | `integration.module_search` |
| Missing below an inaccessible directory remains I/O failure; POSIX search-only directory permits exact lookup/fallthrough | `integration.module_search` |

The independent format oracle builds matching metadata, bytecode and stored ZIP
fixtures; expected hashes do not call the production validator. Required checks
and component-host fixture operations remain active in Release. The shared
scenario framework supplies a unique build workspace, a single deadline,
command/stdout/stderr diagnostics, failure retention and `--keep-work`.
Permission restoration is registered before applying restrictions and attempted
on failures as well as success. Inputs under `test/data` and the format vectors
are read-only.

Platform coverage preserves POSIX FIFO, symlink and mode-bit semantics and
Windows native paths, restricted token and disposable ACL tests. Windows
symlink availability is reported explicitly. Access-denial fixtures may be
unavailable locally only when actual native probing proves the filesystem
ignores mode bits; CI must enforce them. The `module-search-migration` label
is included in Linux/MinGW Debug and Release and Alpine/musl Debug and Release.

```sh
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel 2 --target module_search_scenarios
ctest --test-dir build/manual --output-on-failure -L module-search-migration
build/manual/module_search_scenarios integration.module_search build/manual/module_search_testing build/manual test/data docs/module-format-v1/vectors --keep-work
# Repeat in Release; use .exe on Windows.
# Historical Python parity check (remove the restored script afterwards):
git show 18fa47a266bcc79add896e04563d87991102142d:scripts/check_module_search.py > scripts/check_module_search.py
python3 scripts/check_module_search.py build/manual/module_search_testing
```

The checklist is marked in the implementation commit; final-commit CI links
are recorded in the PR/issue without a second checklist-only push.

Local verification: the historical Python runner and C replacement each pass
51 Linux search fixtures in Debug and Release. Temporary differential capture
matched all 51 prepared/result JSON responses, exit status and stderr, plus
archive metadata and bytecode hashes after normalizing only workspace prefixes.
UBSan passes with Python discovery disabled. Deliberately wrong successful hash
output is rejected in both Debug and Release, retaining command diagnostics.
`--keep-work` retains successful fixtures; ordinary success removes its workspace.
Clang-format 18 and strict C11 `-Wall -Wextra -Werror` syntax checks pass.

## Per-consumer module dependencies (#351)

`module_dependencies_scenarios` replaces `scripts/check_module_dependencies.py`.
The stable `integration.module_dependencies` CTest ID is preserved, as are the
existing `module_dependencies_testing` API host and its direct invocation.
Only the test orchestration changes; language, archive and dependency semantics
remain unchanged. The removed Python runner exports no helpers to other scripts.

| Old scenario/check | Stable CTest ID |
| --- | --- |
| Component API self-tests, direct imports, unused defaults, two versions of the same name, independently owned snapshots | `integration.module_dependencies` |
| Application default does not redirect library version; empty leaf records; snapshot transitive records | `integration.module_dependencies` |
| Explicit version affects only that occurrence; duplicate imports coalesce; unused default is not searched | `integration.module_dependencies` |
| Include imports are direct; unreachable/default and explicit imports coexist; absent default fails despite an available explicit version | `integration.module_dependencies` |
| No transitive flattening; missing transitive module permits direct application binding but fails the library's own binding | `integration.module_dependencies` |
| A → B → C represented by independent edge sets; corrupt transitive archive fails when its consumer is bound | `integration.module_dependencies` |
| Valid replacement with the same name/version changes the independent hash; application accepts it while a committed library hash fails | `integration.module_dependencies` |
| Exact failed-edge index and attempted path; two consumers retain independent hashes for the same version | `integration.module_dependencies` |
| Canonical direct-record order independent of metadata order; failure after the first valid edge exposes no partial result | `integration.module_dependencies` |
| Empty consumer does no archive lookup, retains unused-default count; source-only explicit versions own verified hashes | `integration.module_dependencies` |

The C runner constructs the same bytecode, metadata and ZIP inputs using the
independent test format oracle, not production validators for expected hashes.
Unicode project paths, `gmod`, `module` and `source` modes, successful records,
error/search statuses, absent partial records and snapshot dependencies remain
covered. There are no platform-specific exclusions in this scenario.

The shared scenario framework gives each run a unique build-directory workspace,
a single 180-second deadline, captured command/stdout/stderr diagnostics,
deferred cleanup and failure retention. `--keep-work` retains successful work;
otherwise it is removed. Source data and format vectors are read-only. Required
checks remain active in Release. CTest allows additional time for cleanup.

The `module-dependencies-migration` label is included in Linux x86-64/AArch64,
MinGW i686/x86-64/UCRT Debug/Release and Alpine/musl Debug/Release CI selections.

```sh
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel 2 --target module_dependencies_scenarios
ctest --test-dir build/manual --output-on-failure -L module-dependencies-migration
build/manual/module_dependencies_scenarios integration.module_dependencies build/manual/module_dependencies_testing build/manual test/data docs/module-format-v1/vectors --keep-work
# Repeat in Release; use .exe on Windows.
# Historical Python parity check (remove the restored script afterwards):
git show 1019296d0d7b1228e897044b52150ebeac4b5a0b:scripts/check_module_dependencies.py > scripts/check_module_dependencies.py
python3 scripts/check_module_dependencies.py build/manual/module_dependencies_testing
```

The checklist is marked in the implementation commit; exact final-commit CI
results and links are recorded in the PR/issue, avoiding a checklist-only push.

Local verification: Debug, Release and UBSan runs each pass all 22 bindings.
Temporary differential capture matched all 22 ordered commands, JSON responses,
exit codes and stderr, plus every generated archive's metadata and bytecode
(normalizing only workspace prefixes). Release rejects deliberately emptied
successful dependency records and malformed metadata input, retaining diagnostics.
The component host explicitly keeps its contract assertions enabled in Release.

## Whole module graphs (#352)

`module_graph_scenarios` replaces `scripts/check_module_graph.py`, preserving
the stable `integration.module_graph` CTest ID and the existing
`module_graph_testing` API host. Only test orchestration changes; language,
archive format and graph semantics are unchanged. The old runner exports no
helpers consumed by other scripts.

| Old scenario/check | Stable CTest ID |
| --- | --- |
| API self-tests; empty root, transitive chain, late shared paths and cached search results; root-only search roots | `integration.module_graph` |
| Missing, corrupt and incorrectly named candidates; independent versions; conflicting requests and equal wrong hashes; missing children behind bad hashes | `integration.module_graph` |
| Self-cycle and transitive cycle; broken root; true transitive function owner, sibling isolation, wrong function/version, independent and blocked references | `integration.module_graph` |
| Opaque native preflight without loading; source imports/includes/defaults; ambiguous imports, unnamed empty source, final root overrides installed identity | `integration.module_graph` |
| Complete sorted causal chains; every conflict request and late incoming path; discovery-order independence and repeatable diagnostic text | `integration.module_graph` |
| Shared positioned schema failures, owned field/entry bytes; reference export order independent of JSON order; embedded-NUL keys and lengths | `integration.module_graph` |
| Error-kind ordering after full-chain ordering; finite cyclic diagnostics; canonical back-edge paths distinguish actual cycles; absent observer retains snapshots | `integration.module_graph` |
| Non-UTF-8 ZIP names, newline/backslash fields, ordinary Unicode and escaped display controls; POSIX control characters in physical filenames | `integration.module_graph` |
| Native versus main-hash failure ordering; 2500-edge valid and failing chains with 256 KiB POSIX child stack; exponential paths remain lazy | `integration.module_graph` |

Fixtures use the independent test format oracle for bytecode, metadata hashes
and ZIP construction, never production validators to calculate expected values.
Input vectors and `test/data` are read-only. The scenario framework supplies a
unique workspace under build, one 600-second deadline, captured command/output
diagnostics, deferred cleanup and retained failures. `--keep-work` retains
successful work; otherwise it is removed. CTest allows 60 seconds for cleanup.
Contract assertions in the component host stay active in Release.

Windows runs the same graphs and diagnostics, except POSIX-only physical
filenames with control characters and reduced-stack execution (the historical
Python runner has the same exclusions). Linux x86-64/AArch64 and MinGW
i686/x86-64/UCRT Debug/Release CI select the `module-graph-migration` label,
as do Alpine/musl Debug and Release.

```sh
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel 2 --target module_graph_scenarios
ctest --test-dir build/manual --output-on-failure -L module-graph-migration
build/manual/module_graph_scenarios integration.module_graph build/manual/module_graph_testing build/manual test/data docs/module-format-v1/vectors --keep-work
# Repeat in Release; use .exe on Windows.
# Historical Python parity check (remove the restored script afterwards):
git show a03a219e8a283cc836188062521b193236e28293:scripts/check_module_graph.py > scripts/check_module_graph.py
python3 scripts/check_module_graph.py build/manual/module_graph_testing
```

The checklist is marked in the implementation commit; final-commit CI links
are recorded in the PR and issue without a checklist-only push.

Local verification: historical Python and C each pass 48 POSIX fixture calls in
Debug and Release (47 on Windows, excluding the POSIX filename case). Temporary
differential capture compares all ordered host calls, JSON results, exit codes,
stderr, archive metadata and payload hashes. Normalization is limited to workspace
prefixes and JSON serialization whitespace; malformed ZIP name bytes and flags
are preserved. Both builds reject a deliberately wrong check count and a corrupted
root input, retaining command/output logs and fixtures. Successful ordinary runs
remove their workspace; `--keep-work` retains it. Strict C11 warnings and the
repository-wide clang-format 18 check pass; 50 neighbouring migration/framework
CTest scenarios pass as well.

UBSan also passes with Python discovery disabled
(`CMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE`).

## Immutable module snapshots (#353)

`module_snapshot_scenarios` replaces the orchestration in
`scripts/check_module_snapshot.py`, using the existing `module_snapshot_testing`
API host. No language, VM or module-format behavior changes.

| Old scenario/check | Stable CTest ID |
| --- | --- |
| API contract self-tests; 1, 3 and 11 file values and Unicode labels | `integration.module_snapshot.files` |
| Replace root and transitive archives after READY; pinned values/native bytes stay old; fresh load sees replacement | `integration.module_snapshot.replace` |
| Delete archives after READY; pinned execution survives; fresh load rejects missing dependency | `integration.module_snapshot.delete` |
| Corrupt archives after READY; pinned execution survives; fresh load rejects corruption | `integration.module_snapshot.corrupt` |
| Diamond deduplicates shared dependency and ignores unreachable corruption | `integration.module_snapshot.diamond` |
| Two versions retain distinct values after removing modules | `integration.module_snapshot.versions` |
| Missing/corrupt/hash/identity failures, with and without diagnostics; no partial nodes | `integration.module_snapshot.invalid` |
| Throwing initializer after archive deletion retains values and failure count | `integration.module_snapshot.throw` |
| 512-dependency chain, 513 checks and last value; 256 KiB POSIX child stack | `integration.module_snapshot.deep` |

The independent test format oracle constructs bytecode, metadata hashes and ZIP
fixtures. The shared `scenario_command_barrier` helper waits for the exact READY
prefix before mutating files and only then sends G/EOF. It uses the scenario's
single deadline, captures both streams and stops owned processes even if mutation
fails. Framework self-tests cover successful synchronization and failure paths.
Each scenario has a unique workspace under build, retained diagnostics on failure,
cleanup after success and optional `--keep-work`; input data and vectors remain
read-only. Required checks and component-host assertions stay active in Release.
Windows runs all scenarios, omitting only the POSIX stack limit as before.

The final Python fixture consumer was migrated in #354. Snapshot and identity now
share the independent C constructor in `test/support/module_snapshot_fixture.c`;
the temporary `module_snapshot_fixtures.py` compatibility module is removed.

```sh
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel 2 --target module_snapshot_scenarios
ctest --test-dir build/manual --output-on-failure -L module-snapshot-migration
build/manual/module_snapshot_scenarios integration.module_snapshot.replace build/manual/module_snapshot_testing build/manual test/data docs/module-format-v1/vectors --keep-work
# Repeat in Release; use .exe on Windows.
# Historical Python parity check (remove restored runner afterwards):
git show 08adc43c7a3e8b8ad52c72fe5a22769ed298de5b:scripts/check_module_snapshot.py > scripts/check_module_snapshot.py
python3 scripts/check_module_snapshot.py build/manual/module_snapshot_testing
```

The `module-snapshot-migration` label is selected by Linux x86-64/AArch64,
MinGW i686/x86-64/UCRT and Alpine/musl Debug/Release CI. The checklist mark is
part of the implementation commit; final-commit CI links are recorded in the
PR and issue without an extra checklist-only push.

Local verification: Debug and Release differential capture matches all 21 ordered
Python/C host calls, parsed outputs, exit codes and stderr, plus all archive
metadata and bytecode/native payload hashes before and after synchronized
mutations. Normalization is limited to workspace roots and JSON whitespace.
Release rejects deliberately incorrect check counts and corrupt root inputs,
retaining diagnostics. Ordinary success removes its workspace; `--keep-work`
retains it. UBSan with Python discovery disabled passes all nine scenarios.


## Module identity and serialization (#354)

`module_identity_scenarios` and `module_serialization_scenarios` replace the two
Python runners, preserving the existing component hosts without production changes.
The historical serialization runner actually tests OS-thread initialization
publication/exclusion and direct-entry readiness, not a generic format roundtrip;
its three padding inputs each exercise four component-host variants.

| Old scenario/check | Stable CTest ID |
| --- | --- |
| Shared external cell, same targets and four nodes for identical copies | `integration.module_identity.identical` |
| Informational metadata excluded from identity | `integration.module_identity.information` |
| Unused corrupt physical copy ignored | `integration.module_identity.corrupt-unused` |
| Same name, two versions: counters 2/1 and distinct hashes | `integration.module_identity.versions` |
| First archive has no native data; later native data not adopted | `integration.module_identity.vm-first` |
| First native bytes survive a later VM-only copy | `integration.module_identity.native-first` |
| Changed native bytes/additional variant do not replace first archive | `integration.module_identity.different-native` |
| Later corrupt archive ignored | `integration.module_identity.corrupt-later` |
| Missing first archive selects later native bytes | `integration.module_identity.fallback-missing` |
| Fallback with additional ARM variant preserves both native payloads | `integration.module_identity.extra-fallback` |
| First existing mismatching hash cannot fall back | `integration.module_identity.mismatching-first` |
| Conflicting incoming hashes do not create duplicate instances | `integration.module_identity.hash-conflict` |
| Publication, exclusion, direct-entry readiness; padding 0/2/7 | `integration.module_serialization.padding_0`, `.padding_2`, `.padding_7` |

All scenarios use unique build workspaces, common deadlines and captured command
logs, retain diagnostics after failure and support `--keep-work`. Successful runs
clean their workspaces. Test data/vectors are read-only; assertions remain active
in Release. No platform-specific skips are added. The label
`module-identity-serialization-migration` is included in Linux x86-64/AArch64,
Windows MinGW i686/x86-64/UCRT and Alpine/musl Debug/Release CI.

```sh
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel 2 --target module_identity_scenarios module_serialization_scenarios
ctest --test-dir build/manual --output-on-failure -L module-identity-serialization-migration
build/manual/module_identity_scenarios integration.module_identity.different-native build/manual/module_identity_testing build/manual test/data docs/module-format-v1/vectors --keep-work
build/manual/module_serialization_scenarios integration.module_serialization.padding_7 build/manual/module_serialization_testing build/manual test/data docs/module-format-v1/vectors --keep-work
# Repeat in Release; use .exe on Windows.
# Historical runners and their temporary fixture helper are available at:
# 8558c3e241bee91c8de0ddd5bb7f78d16b304b57:scripts/check_module_identity.py
# 8558c3e241bee91c8de0ddd5bb7f78d16b304b57:scripts/check_module_serialization.py
# 8558c3e241bee91c8de0ddd5bb7f78d16b304b57:scripts/module_snapshot_fixtures.py
```

The checklist mark is included in the implementation commit; final-commit CI
links are recorded in the PR and issue without a checklist-only push.

Local verification: all 12 ordered identity calls match Python/C in Debug and
Release, including parsed JSON results, every archive's metadata and bytecode/native
payload hashes (only JSON whitespace and workspace roots normalized). Serialization
fixtures match exact bytecode and parsed metadata for all three padding values.
Deliberately wrong identity counters/native lists, wrong serialization output and
corrupt metadata are rejected with retained diagnostics, including Release.
All 24 new and snapshot cases pass Debug, Release and UBSan with Python discovery
disabled; 52 combined migration/framework checks pass in Debug. Strict C11 warnings
and the repository-wide clang-format 18 check pass.

## Module initialization and saved failures (#355)

`module_initialization_scenarios` replaces `check_module_initialization.py` and
`check_module_initialization_errors.py`, retaining their component hosts and all
60 wire fixtures. Each row is independently selectable through CTest or the runner.

| Old scenario/check | Stable CTest ID |
| --- | --- |
| Lazy one-shot execution, cyclic include gate, declaration cells/constants, untouched third file, repeated initialization and saved failure | `integration.module_initialization.normal` |
| Same lifecycle checks with caught uninitialized-declaration exception and diagnostic text | `integration.module_initialization.caught` |
| Early declaration access aborts initialization, preserves caller/stack, and caches failure without rerunning | `integration.module_initialization.early` |
| Return interrupts initialization and caches failure without rerunning | `integration.module_initialization.return` |
| Saved mutable object failure survives GC; guarded scope reads/writes, observation, clone/flat and function calls preserve payload; ordinary objects remain usable; contradictory success/failure rejected | `integration.module_initialization_errors.object` |
| Same saved-failure/access checks with null payload | `integration.module_initialization_errors.null` |
| Same saved-failure/access checks with integer payload 9001 | `integration.module_initialization_errors.integer` |

Each initialization mode covers padding 0/2/7, main file 0/1 and internal Unicode
names versus identical external names (12 cases). Each error mode covers padding
0/3 and main file 0/1 (four cases). The independent constructor reproduces the
original bytecode without production writers; metadata is read from the same
read-only vector. No platform-specific skips were introduced. Component-host
assertions explicitly remain enabled in Release.

The shared framework supplies unique workspaces under build, a scenario-wide
120-second deadline (CTest 240 seconds), process cleanup and captured diagnostics.
Failure retains its workspace; success removes it unless `--keep-work` is passed.
Retained `pad-P-main-M-external-E.bin` files contain the exact host stdin for parity.
The `module-initialization-migration` label runs in Linux x86-64/AArch64,
MinGW i686/x86-64/UCRT and Alpine/musl Debug/Release CI.

```sh
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel 2 --target module_initialization_scenarios
ctest --test-dir build/manual --output-on-failure -L module-initialization-migration
build/manual/module_initialization_scenarios integration.module_initialization.caught build/manual/module_initialization_testing build/manual test/data docs/module-format-v1/vectors --keep-work
build/manual/module_initialization_scenarios integration.module_initialization_errors.object build/manual/module_initialization_errors_testing build/manual test/data docs/module-format-v1/vectors --keep-work
# Repeat in Release; use .exe on Windows.
# Historical Python runners: e7b8dfbb5fd8b9964b2de9f800d2401bb03f5689:scripts/check_module_initialization.py
# and the same commit's scripts/check_module_initialization_errors.py.
```

The implementation commit includes the checklist mark; final-commit CI links are
recorded in the PR and issue without a checklist-only push.

Local verification compared all 60 inputs in both Debug and Release: exact
bytecode, parsed JSON metadata, exit status, stdout and stderr match the historical
Python runners. Wrong host output and malformed metadata fail with diagnostics;
corrupt bytecode is rejected by both Release hosts. All seven scenarios also pass
UBSan with Python discovery disabled. The 73-case related migration/framework
selection passes (one intentional framework SKIP), as do strict C11 warnings and
clang-format 18 checks.

## Initialization contract and exception propagation (#356)

`module_initialization_contract_scenarios` and `module_exception_scenarios`
replace `check_module_initialization_contract.py` and
`check_module_exception_propagation.py`. Both retain the existing real VM hosts;
independent wire constructors preserve every original fixture.

| Old scenario/check | Stable CTest ID |
| --- | --- |
| Blocking initializer; original mutable object exception and trace survive GC and eight repeat calls from another file/module; no reinitialization | `integration.module_initialization_contract.object` |
| Same saved-failure contract with null / integer payload | `integration.module_initialization_contract.null`, `.integer` |
| Successful initializer runs once and preserves its scope | `integration.module_initialization_contract.success` |
| Rule 26: locally caught exception | `integration.module_exception_propagation.rule_26` |
| Rule 27: caught child initialization failure | `integration.module_exception_propagation.rule_27` |
| Rule 28: caught transitive initialization failure | `integration.module_exception_propagation.rule_28` |
| Rule 29: throwing function does not run during initialization | `integration.module_exception_propagation.rule_29` |
| Rule 30: parent failure after successful child | `integration.module_exception_propagation.rule_30` |
| Rule 31: direct uncaught failure | `integration.module_exception_propagation.rule_31` |
| Rule 32: transitive failure preserves original trace | `integration.module_exception_propagation.rule_32` |
| Rule 33: catch replacement exception becomes parent failure | `integration.module_exception_propagation.rule_33` |
| Rule 34: finally replaces exception | `integration.module_exception_propagation.rule_34` |
| Rule 35: finally preserves exception | `integration.module_exception_propagation.rule_35` |

Each contract ID covers padding 0/5 and internal names / identical external Unicode
names (16 fixtures total). Each exception rule covers those four combinations and
object/null/integer payloads (120 fixtures). The hosts retain all scope, state,
stack, trace-origin, exception identity, garbage-collection and repeated-call
checks. Assertions remain explicitly enabled in Release.

The common `scenario_command_handshake` sends binary input without closing stdin,
waits for the exact READY event, checks the child is still alive, then sends R and
EOF. The first object fixture holds the already-ready initializer for at least
5300 ms; other fixtures release immediately. Startup is never guessed from a
sleep. Framework tests cover binary input, incorrect READY, early exit and shared
deadlines before READY, during the hold and after release. Windows CRLF is handled
explicitly; no platform skips were added.

Unique workspaces, saved framed inputs, command/stdout/stderr diagnostics,
fail-fast process-tree cleanup, a shared 120-second deadline (CTest 240 seconds),
and `--keep-work` use the existing test support. Input vectors are read-only.
The `module-contract-exceptions-migration` label runs on Linux x86-64/AArch64,
Windows MinGW i686/x86-64/UCRT and musl Debug/Release.

```sh
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel 2 --target module_initialization_contract_scenarios module_exception_scenarios
ctest --test-dir build/manual --output-on-failure -L module-contract-exceptions-migration
build/manual/module_initialization_contract_scenarios integration.module_initialization_contract.object build/manual/module_initialization_contract_testing build/manual test/data docs/module-format-v1/vectors --keep-work
build/manual/module_exception_scenarios integration.module_exception_propagation.rule_33 build/manual/module_exception_propagation_testing build/manual test/data docs/module-format-v1/vectors --keep-work
# Repeat in Release; add .exe on Windows.
```

Historical Python runners are available in commit
`ef45a46c3d8a8877084a114524f8d21dfb554e1a`. The checklist mark is included in the
implementation commit; final CI links are recorded in the PR and issue without
an extra checklist-only push.

Local verification compared all 136 saved wire inputs against Python: identical
bytecode and equivalent metadata, with both implementations passing the same
Debug/Release hosts. Every Release input rejects malformed JSON and corrupt
bytecode; wrong runner output also fails with retained diagnostics. All 14 new
scenarios plus 31 execution-framework cases pass Debug, Release and UBSan with
Python discovery disabled. The 76-case related Debug regression selection and
clang-format 18 checks pass.

## Incomplete scopes and failed prototypes (#357)

`module_incomplete_scopes_scenarios` and `module_failed_prototypes_scenarios`
replace `check_module_incomplete_scopes.py` and `check_module_failed_prototypes.py`.
The real VM hosts are retained, including their scope, file-state, side-effect,
exception identity/trace, access, copy and immutable-bytecode checks. Host
assertions and fixture calls remain enabled in Release.

| Old scenario/check | Stable CTest ID |
| --- | --- |
| Rule 36: pending declaration reads fail without replacing the reserved cell | `integration.module_incomplete_scopes.rule_36` |
| Rule 37: early writes fail; completed declarations retain access/constness | `integration.module_incomplete_scopes.rule_37` |
| Rule 38: observation omits pending/private fields; direct access still fails | `integration.module_incomplete_scopes.rule_38` |
| Rule 39: clone/flat rejects unfinished file scopes, ordinary objects remain copyable | `integration.module_incomplete_scopes.rule_39` |
| Rule 40: inherited early access fails; later access sees completed declarations | `integration.module_incomplete_scopes.rule_40` |
| Rules 41–45: direct prototype | `integration.module_failed_prototypes.shape_0` |
| Rules 41–45: deeper ancestor | `integration.module_failed_prototypes.shape_1` |
| Rules 41–45: shared ancestor (diamond) | `integration.module_failed_prototypes.shape_2` |
| Rules 41–45: healthy first branch cannot hide an incomplete/failed later branch | `integration.module_failed_prototypes.shape_3` |

Each scope ID covers caught/uncaught errors, main file 0/1, padding 0/4, and both
mutable/constant variants (also clone/flat for rule 39): 80 fixtures. Each prototype
ID covers main file 0/1/2, padding 0/5, successful/failed initialization and
object/null exceptions: 96 fixtures. All four shapes preserve checks for own versus
inherited fields, shallow clone identity, full ancestor validation by flat,
no partial result or source mutation, repeated failures, and builtin fallback guards.
The independent constructors preserve Unicode filenames, source locations and
instruction operands without invoking production writers.

The existing scenario framework supplies unique build workspaces, saved framed
inputs, command/stdout/stderr diagnostics, process cleanup, a shared 120-second
deadline (CTest 240 seconds), and `--keep-work`. Source data is read-only.
No platform skips were introduced. The `module-scopes-prototypes-migration` label
is wired to Linux x86-64/AArch64, Windows MinGW i686/x86-64/UCRT and musl
Debug/Release builds, including explicit target lists.

```sh
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel 2 --target module_incomplete_scopes_scenarios module_failed_prototypes_scenarios
ctest --test-dir build/manual --output-on-failure -L module-scopes-prototypes-migration
build/manual/module_incomplete_scopes_scenarios integration.module_incomplete_scopes.rule_36 build/manual/module_incomplete_scopes_testing build/manual test/data docs/module-format-v1/vectors --keep-work
build/manual/module_failed_prototypes_scenarios integration.module_failed_prototypes.shape_3 build/manual/module_failed_prototypes_testing build/manual test/data docs/module-format-v1/vectors --keep-work
# Repeat in Release; append .exe on Windows.
```

Historical Python runners are available in commit
`caaafb8b2fd1addf9889e28d7f945fc1282427b8`. All 176 saved wire fixtures were compared
against Python: byte-identical bytecode and equivalent JSON metadata; old and new
runners pass the same Debug/Release hosts. All nine new scenarios pass Debug,
Release and UBSan with Python discovery disabled. Deliberately malformed JSON,
corrupt bytecode and wrong runner output are rejected in Release with retained
scenario diagnostics. The checklist mark is included in the implementation commit;
PR and final CI links are recorded in the issue without a checklist-only push.

## Basic module access (#358)

`module_access_scenarios.c` replaces `scripts/test_module_access.py`; the existing
`module_access_testing` host retains every model, VM and wire assertion, now also
explicitly enabled in Release. No #359 helpers or language semantics changed.

| Old scenario/check | Stable CTest ID |
| --- | --- |
| `model_cases`: five modifiers across same-file, same-module and foreign callers; denied writes preserve cells; constants, enumeration/stringification, new declarations, shadowing, clone/flat method rights, GC | `integration.module_access.model_vm` |
| `vm_cases`: 23 real VM cases including direct/detached/closure calls, denied callback/read/write, external/public access, clone/flat, added fields and enumeration | `integration.module_access.model_vm` |
| Legacy declarations, 1/2/3/20 files × padding 0/2 (8 images) | `integration.module_access.wire_files` |
| File/public/external modifiers × mutable/constant × padding 0/2 (12 images) | `integration.module_access.wire_declarations` |

Both wire IDs retain independent module instances, every caller/file access matrix,
constant write failures, declaration identity, GC and immutable input checks. The
independent builder uses literal wire values, never production serialization.
All 20 saved C fixtures match the historical Python bytecode exactly and JSON
structurally. Old and new runners pass the same Debug and Release hosts; all three
C scenarios pass UBSan too, with Python discovery disabled. Wrong host output,
malformed JSON and corrupted wire magic are rejected in Release. Failure diagnostics
are retained; successful workspaces are removed unless `--keep-work` is given.

```sh
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel 2 --target module_access_scenarios
ctest --test-dir build/manual --output-on-failure -L module-access-migration
build/manual/module_access_scenarios integration.module_access.wire_files build/manual/module_access_testing build/manual test/data docs/module-format-v1/vectors --keep-work
# Repeat in Release; append .exe on Windows.
```

The three scenarios use unique build directories, read-only source inputs and a
shared 120-second deadline (CTest 240 seconds). The `module-access-migration` label
is included in Linux x86-64/AArch64, Windows MinGW i686/x86-64/UCRT and musl
Debug/Release CI, including explicit build targets. There are no platform skips.
The original Python runner remains available at commit
`0578f90e3847bfd258746e5d91bb6de6692481e1`. The checklist is marked in this
implementation commit; PR and green CI links are recorded in the issue/PR.

## Multifile access (#359)

`module_multifile_access_scenarios.c` replaces `check_module_multifile_access.py`
and the three `module_access_*_cases.py` helpers with 82 stable IDs and one
read-only fixture table. The [complete scenario map, parity results and commands](../../data/module-access/README.md)
cover AC-01–AC-28, include/import and clone/flat variants, static rejection and
public source/binary CLI checks with both optimizers. Label:
`module-multifile-access-migration`.

## Context constants and provenance (#360)

`module_context_scenarios.c` and `module_function_provenance_scenarios.c`
replace the two Python runners with 36 context and 13 provenance CTest IDs.
See the complete [context mapping and parity](../../data/module-context/README.md)
and [provenance mapping and parity](../../data/module-provenance/README.md).
Label: `module-context-provenance-migration`.

## Assignment access (#361)

`module_assignment_scenarios.c` replaces `check_module_assignment_access.py`
with 68 CTest IDs and one read-only fixture table: 42 host projects (90 compile/VM
checks) and 26 CLI checks across both optimizers. Exact rules 13–25 mapping,
parity and negative verification, platforms and commands are documented in
[`test/data/module-assignment/README.md`](../../data/module-assignment/README.md).
Label: `module-assignment-migration`.

## Compiled source diagnostics (#363)

`module_source_diagnostics_scenarios.c` replaces
`check_module_source_diagnostics.py` with ten stable IDs and 25 real host checks.
The relocation pair remains one identity comparison. See
[the mapping, parity and negative controls](../../data/module-source-diagnostics/README.md)
for external file IDs, immutable relocated metadata/bytecode, saved and frozen
traces, UTF-8/control escaping, and the explicit Unix-only filename skip on
Windows. Label: `module-source-diagnostics-migration`.

## Information fields and merge (#364)

`module_information_scenarios.c` replaces both information Python runners with
206 CTest IDs (331 + 240 original checks), including an nm network-symbol audit,
writer golden/idempotence, significant hashes and interactive author ordering.
See [the mapping and validation](../../data/module-information/README.md) and
[complete ID table](../../data/module-information/cases.md). The component writer
host now retains assertion setup/checks in Release; production code is unchanged.
Label: `module-information-migration`.

## Dispatch and consistency (#365)

`module_dispatch_consistency_scenarios.c` replaces `check_module_dispatch.py`
and `check_module_consistency.py` with 144 CTest IDs. Real production dispatch,
ABI actions/guards/fallback, immutable archived metadata/code, copied descriptors
and owner-rooted reference graphs remain checked; producer identity still really
compiles all four optimizer/native combinations. See the
[complete map, parity, negative controls and local cleanup limitation](../../data/module-dispatch-consistency/README.md).
Label: `module-dispatch-consistency-migration`; there are no platform skips.

## Shared module validation (#366)

`module_validation_scenarios` replaces the Python validation runner with 62 stable
IDs under `integration.module_validation.*`, label `module-validation-migration`.
See [mapping, exact-input parity, platform branches and reproduction](../../data/module-validation/README.md).
The component validation host retains its assertion setup in Release; production
code is unchanged. Real constructor compilation/loading is required on every
platform, with no skips.
