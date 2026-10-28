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
