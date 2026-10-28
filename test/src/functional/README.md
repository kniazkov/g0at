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
