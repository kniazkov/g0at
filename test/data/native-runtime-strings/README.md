# Frozen native runtime string cases (#385)

The ten input fixtures are the exact UTF-8/LF bytes written by the unchanged
`test/src/native/checks/runtime_strings.py` at the commit recorded in
`provenance.json`. Capture executed its original `main()` with mocked
`subprocess.run` results, without changing its generator, literals, templates,
loops or file writes. No interpreter or native output produced expected answers.
The original script, `host.c`, `checks.h`, and every fixture have recorded SHA-256
hashes. An independent replay matched all ten original writes byte-for-byte.
The `.c.txt` suffix only prevents source formatters from changing frozen inputs;
work files retain their original `.c` filenames.

The Python checker was retained unchanged through the full cross-platform
original/C parity gate recorded below, then removed. Fixture files have LF line endings; the C runner expands LF
to CRLF when writing on Windows, exactly like the original `Path.write_text`.
Generated `automatic.c` already contains the original inlined `checks.h` and
SDK-local include name. Its bytes are frozen rather than reconstructed by
production string helpers.

## Interface and integration

`native_runtime_strings_scenarios all|ID|--list GOAT REPOSITORY BUILD DATA [--keep-work] [-- COMPILER_FLAGS...]`

DATA is `test/data`. GOAT and REPOSITORY are canonicalized, including symlinks and
Windows path separators, matching the original interpreter's `Path.resolve`.
The runtime and compiler host use archives beside that exact interpreter, not a
separately rebuilt or guessed runtime. The target needs
`goat_language_execution_support` and a build dependency on `goat`; it does not
need yyjson or a production string implementation as an oracle.

The framework creates a unique `scenario-*` directory under BUILD per invocation.
All original work files live directly in that directory. `--keep-work` retains
successful runs as well as failures. `native.txt` and `vm.txt` are raw output bytes
with their original names; `automatic.txt` additionally records automatic backend
stdout. Complete command arguments, stdout, stderr, termination and cleanup
status are saved by the standard scenario framework. The temporary original parity
wrapper compared these files without modifying either checker.

Stable IDs use prefix `integration.native_runtime_strings.`:

- `external-sdk`: compile the exact original `strings.c` against the build's
  `libgoat_native.a`, compile the original memory-debug host, run both the DLL/SO
  driver and unoptimized/native-off VM, and compare all 281 observations.
- `automatic-backend`: compile the original compiler host, compile the frozen
  automatic source using the interpreter's real backend and embedded SDK/runtime,
  compare all 281 observations with the VM, and reject any remaining
  `automatic.*.goat-*` staging entry. For this independent selection the reference
  VM is executed first; `all` preserves the original complete ordering.
- `invalid-0`, `invalid-1`: negative and INT64_MAX `charAt` indices.
- `invalid-2`, `invalid-3`, `invalid-4`: negative, reversed, and INT64_MAX substring
  ranges.
- `invalid-5`, `invalid-6`: negative and INT64_MAX repeat counts.
- `all`: execute external SDK/native/VM checks, the automatic backend, and all
  seven invalid checks exactly once in original order, with one shared deadline.
  Negative controls are separate CTest cases and excluded from `all`.

The 281 VM observations preserve all original empty, ASCII, whitespace,
Cyrillic, supplementary-plane, embedded-NUL, accented, and Unicode-space inputs;
length, empty/blank checks, trim, case mapping, contains, index/last-index,
prefix/suffix, repeat, concatenation, substring, character access, overlapping
replacement, format-index overflow/leading zeros/missing arguments, and integer,
float, boolean and character-to-string conversion cases.

Both native paths run all original `checks.h` assertions: invalid status and
unchanged outputs, overflow before allocation/dereference, non-terminated views,
embedded zeros, borrowed slices, release and double release, empty huge repeat,
truncated/missing ABI services, INT64_MIN formatting, and a deliberately live
host-owned allocation. The unchanged host checks allocation growth and exact
baseline restoration after bulk cleanup across the shared-library boundary.
These independently stated constants and ownership checks remain the oracle;
production functions never calculate expected answers. VM comparison is retained
only where the original used it.

The output predicate implements Python **bytes.splitlines()**, which recognizes
only CR, LF and CRLF, preserves empty interior lines and embedded NULs, and ignores
one final terminator. It does not decode UTF-8 or apply text/Unicode splitlines
rules. Invalid calls must exit nonzero and contain the exact `INVALID_ARGUMENT`
byte sequence in stderr; the original imposes no additional stdout restriction.

## Commands, instrumentation and deadlines

All children inherit the environment unchanged and run from REPOSITORY. `CC`
(default `gcc`) and `AR` (default `ar`) remain single, unsplit environment values,
including an explicitly empty value. Original argument order and flags are kept:
`-std=c11 -O2 -Wall -Wextra -Werror -pedantic-errors`, original include paths,
shared/static Windows versus PIC Unix options, memory-debug host, compiler-host
libraries, and all caller-supplied flags. These driver compilations do not inherit
CMake Release's `-DNDEBUG`; original assertions stay active in Debug and Release.
The scenario runner itself uses always-active requirements rather than `assert`.

Any explicit compiler flags select the original instrumented-runtime path:
`src/native/strings.c`, `src/native/str.c`, `lib/numbers/number_format.c`, and
`lib/strings/unicode.c` are each rebuilt with those flags into the original
`*-runtime.o` filenames, then archived using AR into
`libgoat_native-instrumented.a`. The shared module and host receive the same
flags. Like Python, `all` omits the automatic backend in this mode because it
cannot forward those instrumentation flags into the real compiler backend.
Selecting `automatic-backend` explicitly with flags reports a skip (77).
Example: append `-- -fsanitize=undefined -fno-sanitize-recover=all`.

Each host, VM, automatic-host and invalid invocation retains its original
90-second cap. Compiler and archive commands use the shared scenario deadline,
without a new per-command deadline. `all`, `external-sdk`, `automatic-backend`,
and `negative-host` have 300-second totals, preserving the original manifest's
300-second aggregate cap. Individual invalid and negative-output scenarios have
90-second totals; negative-invalid has 180 seconds and negative-cleanup has
10 seconds. CTest must add a cleanup margin outside these deadlines. Timeouts,
crashes and supervisor cleanup errors always fail, even when the child exits zero.

## Negative controls

- `negative-output`: obtain actual VM output, require a passing baseline, then
  reject a changed byte and nonzero status with the same output predicate.
  Fixed independent byte vectors test CR/LF/CRLF equivalence, final versus extra
  blank lines, empty input, embedded NULs, non-UTF-8 bytes, and rejection of false
  equivalence for VT, FF, ASCII record separator, NEL and Unicode line separator.
- `negative-invalid`: require an actual invalid call to pass the error predicate,
  then reject success and a wrong diagnostic; preserve binary substring matching
  around NULs. A real successful VM program must also be rejected by that checker.
- `negative-host`: compile and run all original native operations and boundary
  assertions as a passing baseline. Compile a second module that corrupts the
  copied buffer length immediately before its original independent length/content
  assertion. Assertion failure exits normally with sentinel 86 in these two
  modules only. The bad module must exit exactly 86 and fail normal host
  acceptance. Crashes, missing tools, timeouts and cleanup failures cannot satisfy
  this control. The original fixtures and host are not modified.
- `negative-cleanup`: require an initially clean directory, accept unrelated
  filenames, and reject real leftover files and directories matching the exact
  original glob; removal restores the good baseline. Windows also checks
  case-insensitive matching, like `Path.glob` on that platform.

## Local verification

The unchanged Python checker passed with both Debug and Release interpreters.
The focused C Debug pass completed 12 of 13 independent scenarios successfully,
including all four negatives. The automatic-backend and aggregate runs failed
with the known restricted-environment supervisor `cleanup=2` after the automatic
child exited zero. They are not counted as passes and no supervisor check was
weakened or bypassed.

The explicit UBSan aggregate completed all original applicable checks, rebuilding
all four runtime objects and both drivers. Its 2,589-byte native and VM output
files each exactly matched the original Debug checker. Strict C11 compilation
with `-DNDEBUG -Wall -Wextra -Werror -pedantic-errors` and clang-format 18 checking
also pass. Integrated CMake/CTest and remote cross-platform parity results are
reported separately; the originals remain present pending that gate.

Integrated local gate (2026-11-03): CMake Debug and Release builds passed, and
both configurations passed 25/30 `native-strings-migration` CTests. The five
failures in each configuration are the separate automatic-backend selection,
three real method cases and the method original/C gate; every failure records
child exit zero and supervisor `cleanup=2`. The runtime original/C aggregate
and all 16 negative controls passed in this single integrated run. Both dedicated
UBSan CTests passed, including unchanged-Python/C stdout and generated-byte parity.
No failed case was retried just to obtain a local pass. Full remote parity remains
required before deleting the original checkers.

## Remote parity and retirement

Exact parity head `ac2a69a6db9ccfb196bb5b957d4d48cefbfd1822` passed
[Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41597613910) and
[vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41597613872).
All nine Linux GCC/Clang, Windows MINGW32/MINGW64/UCRT64 and native musl jobs
ran both unchanged original/C aggregate gates and all 28 new C scenarios in
Debug and Release. Each gate passed twice per job; the four Linux jobs also
passed the explicit UBSan original/C gate. All generated-input and output/report
byte comparisons passed, including Windows CRLF expansion. No retry was needed.

After that full gate, both original Python checkers, their last imported
`scripts/check_native.py` compatibility helper, and the temporary parity wrapper
were removed together with checklist item 67's completion mark. Original scripts
remain available at the parity head for independent provenance reproduction.
The permanent CTest IDs, manifest adapters, sanitizer invocation and complete CI
coverage remain; the final-removal commit must pass its own full CI separately.
