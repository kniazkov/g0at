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
