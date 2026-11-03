# Native saturation migration (#386)

This read-only corpus preserves the historical independent Python reference
`test/src/native/checks/saturation.py` and its provenance. The C runner
`test/src/functional/native_saturation_scenarios.c` now supplies all 29 selectable
cases. The original Python script and temporary parity wrapper were removed
only after the complete original/C parity gate passed at commit
`38a029e15a4fab21942a3782dc14ef92b3d48892`; see the evidence below. The manifest's
logical `checks/saturation.py` identifier now routes to the C aggregate and does
not require a Python runtime. Final-removal CI is a separate pending gate.

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
missing final newlines where the Python source omitted them. The historical parity wrapper
used `file(READ ... HEX)` to compare actual source/driver bytes; text-mode CMake
reads would hide CRLF differences. Frozen files and generated `expected.txt`
retain an LF baseline on every platform; the historical wrapper compared expected output
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
The historical complete original/C parity wrapper capped the unchanged Python process at
300 seconds. The C runner still enforces its internal 300-second aggregate
budget; its external process timeout is 330 seconds to allow supervisor cleanup.
The historical parity CTest outer limit was 660 seconds (300 + 330 + 30). These cleanup
margins do not extend a child's original execution cap or the C aggregate budget.

Retained workspaces contain command/output diagnostics and generated artifacts.
A known local child-supervisor cleanup `ENOENT` can block completion of the full
C runner even though the real original Python run passes. Treat that as an
unresolved infrastructure failure: preserve its diagnostics, fix or reproduce
it in the intended environment, and rerun the unchanged tests. Do not bypass
the supervisor, suppress the failure, increase deadlines, or claim full parity
from the original run alone. The remote parity gate below passed independently
of this local limitation; final-removal CI is not claimed here.

## Current C reproduction

Run from the repository root with CMake and the selected C compiler available.
These saturation scenarios do not require Python; other transitional repository
tests may still use it. For a single-config Linux example:

```sh
CC=gcc cmake -S . -B build/saturation-debug -DCMAKE_BUILD_TYPE=Debug
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
```

Repeat with `-DCMAKE_BUILD_TYPE=Release` in a separate build directory. The
normal `scripts/build.sh` and `scripts/build_mingw.cmd` explicit target lists
include this runner. Linux and Windows Debug/Release workflow selections and
`scripts/check_musl_ci.sh` Debug/Release selections include the migration label,
which now includes all 29 C cases, without a Python parity registration.
On Windows use the corresponding `.exe` paths and shell path conventions.

## Completed original/C parity gate and retirement

[PR #464](https://github.com/kniazkov/g0at/pull/464) retained the unchanged original
Python checker, legacy Python manifest route and temporary comparison wrapper
until the exact parity head `38a029e15a4fab21942a3782dc14ef92b3d48892` passed:

- [Build and test, attempt 4: 11/11 jobs](https://github.com/kniazkov/g0at/actions/runs/41623617966/attempts/4).
- [Vectors: 3/3 jobs](https://github.com/kniazkov/g0at/actions/runs/41623617722).
- The full musl Debug and Release selections each passed 1,937/1,937 tests.
  Their complete original+C parity tests took 340.29 and 364 seconds,
  respectively, on attempt 4.

At that head the migration label covered 29 split cases plus the temporary
original/C comparison, 30 tests total. The wrapper created a unique
`native-original-saturation/scenario-<nonce>` base, ran the unchanged Python
script in `original`, then ran C `all --keep-work` in its own
`converted/scenario-*` workspace. Both complete executions had to succeed. It
compared `saturation.goat`, `driver.c`, six valid and eight invalid source files
using raw HEX, and compared C-generated expected output with the frozen Python
result. It retained both stdout/stderr captures and generated workspaces. A
passing `corpus` check alone was never accepted as full parity evidence.

The historical wrapper and original can be inspected without restoring live
Python dependencies:

```sh
git show 38a029e15a4fab21942a3782dc14ef92b3d48892:test/run_native_saturation_original.cmake
git show 38a029e15a4fab21942a3782dc14ef92b3d48892:test/src/native/checks/saturation.py
```

The subsequent retirement change removes the Python checker and wrapper,
switches legacy manifest execution to the C aggregate, and marks checklist
step 68 complete. All check-program adapters now execute C. Final CI must still
validate this removal commit; the successful parity-head runs are not a claim
that final-removal CI has passed.

Earlier GCC builtin-driver compilation hit the unchanged 120-second command
limit intermittently. The successful rerun does not establish that this
flakiness is fixed, and its deeper cause remains unknown. No original deadline,
assertion, compiler flag, or cleanup check was relaxed to obtain the green gate.


## Musl compiler resource diagnostics

The first final-removal run again reached the unchanged 120-second builtin
compiler limit on musl AArch64: the split helper case failed, while the later
complete C manifest execution passed in the same job. To distinguish compiler
CPU cost from scheduling, throttling, or memory pressure, the musl CI script
runs a read-only C observer during the existing Debug and Release CTest
selections. Every five seconds it records monotonic/wall timestamps, CPU and
memory counters, pressure and cgroup limits/events, and compiler-process CPU,
resident-memory, state, and context-switch counters. It does not inspect
arguments or environment, attach to processes, or change compiler commands,
priorities, test ordering, or deadlines. The observer is stopped and reaped
after CTest and on script exit. Its failure can fail otherwise-successful CI,
but never converts a failed test into success or replaces its failure status.

Artifacts retain `compile-resources-debug.log` and
`compile-resources-release.log` beside existing nested test diagnostics,
including the exact generated `driver.c`. Missing proc/cgroup files are logged
as unavailable. `/proc/stat` and pressure can reflect host-wide activity;
cgroup counters describe the container's scope. Correlate CPU jiffies by PID
and process starttime with the monotonic interval and CTest results. A process
disappearing does not prove success, and five-second sampling cannot resolve
exact compiler completion time. These samples are diagnostic evidence, not a
workaround or a claim that the intermittent compiler delay has been fixed.

### AArch64 memory-detail observations

The diagnostic final head `7380e1ecd6aa3367f9915a9fd801266494023683`
passed [all 11 Build jobs](https://github.com/kniazkov/g0at/actions/runs/41654522348)
and [all three vector jobs](https://github.com/kniazkov/g0at/actions/runs/41654522178).
However, after merge, the exact master head
`bb07744c4af16506a88fb6cf569196392b08a92b` again failed the AArch64 musl
builtin compiler deadline in both the split and complete manifest cases
([job and retained artifacts](https://github.com/kniazkov/g0at/actions/runs/41664411262/job/126015783384)).
This is a remaining master-gate failure, not evidence that the flakiness was fixed.

The failed `cc1` processes each consumed about 115 seconds of CPU in a
115-second sampled span: about 35 user seconds and 80 system seconds, with
45.8 million minor page faults and no major faults. Successful prior Debug
compiles already had the same kernel-heavy pattern: about 30 user seconds,
65 system seconds and 46.3 million minor faults over 95-second sampled spans.
The failed spans showed no recorded cgroup throttling, OOM, memory-pressure,
host steal or I/O-wait increase. These are sampled observations, not complete
process lifetimes, and they do not identify the source of the minor faults.

On AArch64 only, the existing observer now adds virtual-memory, anonymous/file
RSS, page-table and swap fields, host `vmstat` fault/reclaim/THP/NUMA counters,
and read-only transparent-huge-page configuration. Every sixth sample (roughly
30 seconds), it summarizes each observed `cc1` memory map and reads selected
`smaps_rollup` fields. Map summaries retain counts, sizes and a change
fingerprint, not addresses or file paths. An access denial disables all further
maps/smaps attempts for that observer; it does not request capabilities or
change access settings. Missing files and normal process-exit races remain
visible. Each sample records the observer's own wall and CPU cost so collection
overhead is not assumed to be free. The artifact also records compiler/package
identity, compiler and libc hashes, and page size without compiling another
program.

Growing mapping/RSS totals can distinguish growth from repeated page reuse;
mapping changes and kernel reclaim/THP counters provide additional correlation.
Stable mappings do not rule out reuse between samples or `MADV_DONTNEED` on
existing ranges. Proc reads are not atomic, host counters include other work,
and neither mapping fingerprints nor minor-fault counts identify a call site.
In particular, [GCC 14.2's collector](https://github.com/gcc-mirror/gcc/blob/releases/gcc-14.2.0/gcc/ggc-page.cc)
contains both unmapping and `MADV_DONTNEED` paths, while musl's allocator also
uses mappings. These are investigation candidates, not an established cause.
No tracing, preloading, new package, security change, additional test/compiler
run, deadline extension, driver change or compiler-flag change is introduced.

### Isolated AArch64 compiler-pass replay

The subsequent diagnostic replay is explicitly separate from the normal gate.
After the normal musl job has finished or failed, AArch64 runs the existing
`helpers-builtin` C scenario once in a private container temporary directory.
Its generated scenario is archived as
`/results/compiler-pass-replay/scenario.tar.gz`, so private scenario directories
do not prevent the host artifact uploader from reading the results. Only this
additional invocation uses a compiler wrapper adding `-ftime-report`,
`-ftime-report-details` and `-fmem-report`. The wrapper immediately executes the
same `gcc`; `-O2`, the frozen 540,477-byte driver, all 7,176 assertions and the
existing 120-second command/300-second scenario limits are unchanged. It uses
the existing scenario process-tree cleanup, not a separate timeout mechanism.

The replay retains GCC's report in its scenario `command-2.stderr`, with its
command log, generated files, complete runner output and a separate resource
observation outside the archive. `status.log` records the replay, observer and
archive exit statuses independently; archive failure never masks the replay
status. A reporting timeout is evidence of an incomplete report, never a
successful compile. The original job exit status remains authoritative even if the
additional replay passes. If the original build failed before producing the
required executables, no replay is attempted.

This adds at most one bounded helper scenario after the normal test workload,
including a second compiler process. Reporting and resource collection have
their own overhead; replay timing must not be presented as unchanged-gate
timing or proof of stability. No other test runs concurrently with the replay
in the container. Compiler pass totals can identify where time is accounted,
but some AArch64 passes have no individual time variable, and GGC reports do
not account for every ordinary heap allocation. No optimization is disabled
and no allocator, kernel or security setting is changed.
