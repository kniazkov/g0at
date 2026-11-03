# Native saturation migration (#386)

This read-only corpus preserves the independent Python reference in
`test/src/native/checks/saturation.py` while
`test/src/functional/native_saturation_scenarios.c` adds independently selectable
C scenarios. The Python script and its original native-manifest routing remain
in place during parity validation. The migration checklist must remain unchecked
until the complete required CI matrix passes; this directory is not evidence of
that final gate having passed.

## Exact original-to-C coverage

All case names below have the stable CTest prefix
`integration.native_saturation.` and label `native-saturation-migration`.

| Original operation | CTest suffix | Preserved contract |
| --- | --- | --- |
| `--optimize none --native off --save-native` | `vm-none-off` | Exact 658-line stdout and successful exit |
| `--optimize all --native off --save-native` | `vm-all-off` | Same exact expected stdout |
| `--optimize all --native auto --save-native` | `vm-all-auto` | Same stdout; successful native report contract |
| `--optimize all --native required --save-native` | `vm-all-required` | Same stdout; successful native report contract |
| `--compile --native required`, then `--run --native required` | `persisted` | Successful compilation and exact saved-bytecode output |
| Export C, compile and execute the original driver | `helpers-builtin` | All generated-C and VM integer helper assertions |
| Same driver with `GOAT_FORCE_PORTABLE_INTEGER_MATH` | `helpers-portable` | All assertions using portable helper implementations |
| `minimum.goat`, optimize `none` and `all` | `valid-minimum` | `INT64_MIN` literal |
| `spaced_minimum.goat`, both optimizations | `valid-spaced_minimum` | Whitespace between unary minus and minimum magnitude |
| `double_negation.goat`, both optimizations | `valid-double_negation` | Saturating negation of the minimum |
| `division.goat`, both optimizations | `valid-division` | Minimum divided by minus one, remainder, and real division |
| `order.goat`, both optimizations | `valid-order` | Saturation at the original evaluation boundaries |
| `unchanged.goat`, both optimizations | `valid-unchanged` | Shift, exponentiation, and real-literal behavior |
| `invalid0.goat`: `9223372036854775808` | `invalid-0` | Nonzero exit and `64-bit` in stderr |
| `invalid1.goat`: `-9223372036854775809` | `invalid-1` | Same invalid-literal contract |
| `invalid2.goat`: `18446744073709551616` | `invalid-2` | Same contract |
| `invalid3.goat`: 200 digits of `9` | `invalid-3` | Same contract |
| `invalid4.goat`: `+9223372036854775808` | `invalid-4` | Same contract |
| `invalid5.goat`: `1-9223372036854775808` | `invalid-5` | Same contract |
| `invalid6.goat`: `-(9223372036854775808)` | `invalid-6` | Same contract |
| `invalid7.goat`: `-9223372036854775808**1` | `invalid-7` | Same contract |

The four VM cases preserve all 196 ordered pairs from the 14-element edge
Cartesian product, in the original order, for addition, subtraction and
multiplication. They also preserve five operations on each edge: negation,
absolute value, increment, decrement and `MAX + a` following saturated
`MIN / -1`. Thus the expected output has `196 * 3 + 14 * 5 = 658` lines.

The compiler driver extends those 196 pairs with the unchanged 1,000 ordered
pairs from `test/data/python-samples/saturation-pairs.json`. It tests 1,196 pairs
in order, three operations per pair, and two helper families per operation:
`g_i64_add/sub/mul` and `add/subtract/multiply_int64_saturating`. This is
**7,176 assertions per builtin or portable variant**, or 14,352 across both.
Operands remain `volatile int64_t`. No random values are generated at test time.
The six valid snippets each run twice, and all eight invalid snippets run once.

The `all` CLI selection executes the complete original sequence in one isolated
workspace: four VM commands, two persisted-bytecode commands, one C export, two
compiler/executable pairs, twelve valid commands and eight invalid commands.
That is 31 child commands, comprising 23 ordinary commands and eight invalid
commands. It does not substitute the additional negative controls for original
coverage. Standalone helper cases each perform their own C export for isolation.

## Independent oracle and frozen provenance

`goat_integer_oracle` evaluates exact arithmetic using a sign and unsigned
128-bit magnitude represented by four 32-bit limbs. It clamps only after the
exact operation. It uses neither production integer helpers nor compiler
extended integer types to calculate expected results. The production helpers
appear only on the tested side of the generated driver's assertions.

`provenance.json` records the original script's commit, SHA-256, capture method,
and byte count and SHA-256 of every frozen artifact. The baseline was captured
by executing the unmodified original Python script with a return trace that
observed its independent expected text. All 31 original commands passed in that
Debug capture. `saturation.goat` and `driver.c.txt` are the resulting generated
source bytes, `expected.txt` is the Python-calculated 658-line output, and the
six `.expected` files hold the original valid-snippet answers. The eight invalid
sources and six valid sources retain their original bytes and filenames.

The `corpus` scenario independently regenerates source, driver, and expected
output using the C oracle and compares all three against these frozen files.
It checks that the external sample JSON contains exactly 1,000 two-element
arrays of signed-64-bit-representable integers. Fixture bytes are never updated
by an ordinary test. `.gitattributes` disables checkout newline conversion for
this directory. Deliberate fixture changes require a reviewed reference capture
and updated provenance, not accepting a new C-produced golden.

## Compiler, text, and report contracts

The driver compiler is the raw `CC` environment value, or `gcc` only when `CC`
is absent, matching Python's `os.environ.get('CC', 'gcc')`. It is one executable
argument, not a shell command or a split compiler-wrapper string. Flags remain
`-std=c11 -O2 -Wall -Wextra -Werror`, repository `-I`, `-iquote` for
`lib/numbers`, the driver, `-lm`, and `-o`. The portable variant additionally
passes `-DGOAT_FORCE_PORTABLE_INTEGER_MATH`. Neither variant adds `-DNDEBUG`:
the driver's assertions must remain enabled even when Goat is a Release build.
Successful compiler and driver commands need a zero exit; the original does not
require their stdout or stderr to be empty. `GOAT_LANGUAGE=en` is set for child
commands while preserving the remaining environment.

Output checking uses strict UTF-8 decoding and universal-newline conversion of
CRLF and bare CR to LF, followed by exact comparison. The original Python uses
its default locale encoding for subprocess text decoding; the C runner uses
UTF-8 explicitly. Original fixtures and expected results are ASCII, and the
supported CI environments use UTF-8 locales. This is not a claim of decoding
equivalence for arbitrary legacy-codepage diagnostics. It does not trim spaces, omit trailing newlines, collapse blank
lines, or discard embedded NUL bytes. Successful commands may write stderr.
For invalid cases, the original condition remains nonzero exit plus the
`64-bit` substring in stderr.

Source-file generation is a separate contract. The C writer reproduces
`Path.write_text` platform translation, including LF-to-CRLF on Windows and
missing final newlines where the Python source omitted them. The parity wrapper
uses `file(READ ... HEX)` to compare actual source/driver bytes; text-mode CMake
reads would hide CRLF differences. Frozen files and generated `expected.txt`
retain an LF baseline on every platform; the wrapper compares expected output
using exact HEX bytes without newline normalization.

Auto/required reports preserve the original narrow checks: integer `succeeded`
is positive, `attempts` and `succeeded` values are textually equal, and `retries`
is exactly `0`. The dedicated parser preserves the original report parsing
semantics, including duplicate-key replacement and supported Python-style
integer text and line separators. Unrelated report fields do not introduce new
requirements. Original and C executions each validate their own report;
legitimate native-attempt count differences mean reports are not byte-compared.

## Additional controls

These eight registrations supplement, rather than replace, the original cases:

- `corpus`: validate sample shape and exact independently regenerated frozen
  source, driver and expected bytes without executing Goat.
- `control-output`: execute a real VM result, reject corrupted expected output
  and bad exit status, and exercise newline, trailing-line, NUL and UTF-8 checks.
- `control-report`: accept supported original report forms and reject zero
  successes, unequal attempts, retries, missing/malformed fields and bad UTF-8.
- `control-input`: reject wrong pair counts, wrong tuple lengths, floating-point
  or out-of-range operands, null/string values, and malformed JSON.
- `control-oracle`: corrupt a computed expected value and require a nested
  scenario failure at the frozen-corpus expected-byte comparison, before the
  continuation marker, with retained diagnostics. This control does not compile
  or run a host driver.
- `control-host`: mutate each of the two helper families and require the compiled
  driver to fail with exit 23 in both builtin and portable variants.
- `control-invalid`: reject a falsely successful invalid result, a missing
  diagnostic, and a real valid program; preserve substring matching through NUL.
- `control-execution`: exercise actual self-child commands through the same
  `run()` and `output()` paths. Accept normal output with nonempty stderr, reject
  a nonzero exit and fail fast on mismatched output. Hanging-child probes give
  the command the unchanged 120-second or 30-second allowance, then demonstrate
  that a short 5-second enclosing deadline clips that allowance. Raw
  partial stdout/stderr and termination/cleanup diagnostics are retained. These
  probes do not change any original scenario command deadline.

## Deadlines and failure evidence

Each ordinary child keeps its original **120-second** cap; each invalid-literal
child keeps **30 seconds**. The `all` scenario retains the old manifest's
**300-second aggregate** budget, rather than summing individual command caps.
CTest case timeouts are 330 seconds, except `helpers-builtin`,
`helpers-portable`, and `control-host`, which have 390-second outer limits.
The complete original/C parity wrapper caps the unchanged Python process at
300 seconds. The C runner still enforces its internal 300-second aggregate
budget; its external process timeout is 330 seconds to allow supervisor cleanup.
The parity CTest outer limit is 660 seconds (300 + 330 + 30). These cleanup
margins do not extend a child's original execution cap or the C aggregate budget.

Retained workspaces contain command/output diagnostics and generated artifacts.
A known local child-supervisor cleanup `ENOENT` can block completion of the full
C runner even though the real original Python run passes. Treat that as an
unresolved infrastructure failure: preserve its diagnostics, fix or reproduce
it in the intended environment, and rerun the unchanged tests. Do not bypass
the supervisor, suppress the failure, increase deadlines, or claim full parity
from the original run alone. No final cross-platform CI success is claimed here.

## Reproduction and complete parity gate

Run from the repository root with CMake, Python 3 and the selected C compiler
available. For a single-config Linux example:

```sh
CC=gcc cmake -S . -B build/saturation-debug -DCMAKE_BUILD_TYPE=Debug \
  -DGOAT_TEST_REQUIRE_PYTHON=ON
cmake --build build/saturation-debug --target native_saturation_scenarios
ctest --test-dir build/saturation-debug --output-on-failure \
  -L native-saturation-migration
```

Direct CLI arguments are `all|ID|--list GOAT REPOSITORY BUILD DATA [--keep-work]`.
Use a full stable ID for a single case, and a writable build base for isolated
`scenario-*` workspaces:

```sh
build/saturation-debug/native_saturation_scenarios --list \
  build/saturation-debug/goat "$PWD" build/saturation-debug test/data
build/saturation-debug/native_saturation_scenarios \
  integration.native_saturation.corpus \
  build/saturation-debug/goat "$PWD" build/saturation-debug test/data --keep-work
CC=gcc build/saturation-debug/native_saturation_scenarios all \
  build/saturation-debug/goat "$PWD" build/saturation-debug test/data --keep-work
ctest --test-dir build/saturation-debug --output-on-failure \
  -R '^migration.native_saturation.original-parity$'
```

`test/run_native_saturation_original.cmake` creates a unique
`native-original-saturation/scenario-<nonce>` base. It first runs the unchanged
Python script in `original`, then the C `all --keep-work` selection in its own
`converted/scenario-*` workspace. Both complete runs must succeed. It compares
`saturation.goat`, `driver.c`, all six valid source files and all eight invalid
source files in raw HEX, and checks C-generated expected output against the
frozen Python result. Both stdout/stderr captures and workspaces remain for
review. A passing `corpus` check alone is not this parity gate.

Repeat with `-DCMAKE_BUILD_TYPE=Release` in a separate build directory. The
normal `scripts/build.sh` and `scripts/build_mingw.cmd` explicit target lists
include this runner. Linux and Windows Debug/Release workflow selections and
`scripts/check_musl_ci.sh` Debug/Release selections include the migration label,
which includes all 29 split cases and the complete original/C parity test when
Python is available: 30 CTests in total. Require Python during this transitional
gate; silently omitting the original parity test is not acceptable evidence for retirement.
On Windows use the corresponding `.exe` paths and shell path conventions.

Only after the complete required Debug/Release Linux, Windows and musl matrix
is green may a subsequent change retire the original Python script and temporary
parity wrapper, switch legacy manifest execution to the C aggregate, and mark
the migration checklist complete. Until then keep the Python reference, old
routing and unchecked checklist intact.
