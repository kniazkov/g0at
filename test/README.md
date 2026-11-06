# Running the test suites

Configure from the repository root, build all targets, then run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build -N
ctest --test-dir build --output-on-failure
```

CTest registration is in `test/CTestLists.cmake`. Migrated C runners preserve
stable suite IDs or the documented individual scenario mappings. The original
59 integration registrations have expanded into independently selected C cases;
use `ctest -N` for the current inventory. CI also retains its specialized native,
sanitizer, fault, pattern-initialization, Release/package and controlled
glibc/musl container checks.

Labels select `core`, `integration`, `native`, `format`, `sanitizer`, `fault`,
`package`, `performance`, `style`, `docs` and `graph`. For example:

```sh
ctest --test-dir build --output-on-failure --parallel 4 -L integration
ctest --test-dir build --output-on-failure -LE performance
ctest --test-dir build --output-on-failure -L performance
```

Legacy runners share some resources, so registrations use `RUN_SERIAL` and a
bounded timeout. Performance is run separately in CI and must not compete with
other jobs on the same machine. The individually registered C framework/process scenarios use owned workspaces
and process-local state, and run with `--parallel 4` in Linux and Windows CI.
The Debug integration step also uses four CTest workers on Linux and Windows.
CTest still honors `RUN_SERIAL` for legacy suites and timing-sensitive process-exit
scenarios; the dedicated Release performance step remains exclusive.
Release builds in Linux and Windows CI also use four compiler jobs: the release
script steps set `CMAKE_BUILD_PARALLEL_LEVEL=4`, and the subsequent test-target
builds pass `cmake --build ... --parallel 4`. Each build finishes before tests start.
Functional inputs are copied under the selected build directory; failed copies
are retained with their path in the output. Inputs under `test/data` stay read-only.

The active CMake, CTest and CI gates no longer require Python after #394.
Bash remains required for shell-based suites; missing optional tools produce
explicitly disabled tests and configure messages. On Windows use the MSYS2
environment. CTest supplies the configured C compiler and absolute target paths.
The four inactive frozen-input reproduction/audit scripts were removed in #395.
Their historical sources and reproduction environment remain documented without
reintroducing a Python build dependency. See the
[complete migration inventory and audit](../docs/c-tooling-migration-checklist.md), the
[reference retirement map](data/format-reference/README.md) and
[frozen-input integrity gate](support/frozen_samples.md).

Platform variants are explicit: `native.windows_*` only on Windows;
`native.native_library`, sanitizer variants and GCC x86-64 x87 variants only on
supported Linux configurations. `fault.module_memory` exists only in a separate
`GOAT_TEST_ALLOC_FAILURE=ON` build. Style requires clang-format 18, docs Doxygen,
and graphs Graphviz. Unavailable optional-tool suites are marked disabled.

`package.relocated` requires building the `package` target and reconfiguring after
archives exist. Its absence is reported explicitly. Release CI retains its
existing limited host set and does not invoke Debug-only assert-based hosts as a
replacement for its release checks. Build all test targets before an unrestricted
CTest run. `BUILD_TESTING=OFF` disables registrations but preserves direct runners.

The runtime-floor and musl producer/consumer jobs require controlled Docker
userspaces, prerequisite artifacts and tool installation. They remain external
CI orchestration, not default CTest commands; registering the ordinary requirement
audit does not replace those jobs. Build scripts and `create_test.cmd` are tools,
not tests, and are not invoked recursively from CTest.

### Tooling dependency policy

Build scripts, generators, test runners and packaging must remain independent
of Python. New tooling uses C, CMake or the existing shell/cmd/PowerShell entry
points; this is a development and review rule.

The migration's temporary interpreter guards and dependency-audit controls were
retired after #395 passed its full CI cycle. Normal CI runs the project commands
directly, without altering PATH or auditing interpreters installed on the host.
Frozen reference data and their provenance remain inputs to the existing C tests.

CI still verifies that the normal cycle leaves the checkout clean. Checkout
status and Windows Git diagnostics are stored under `build/ci`; failed scenario
workspaces and the existing build/test artifacts retain their usual locations.

### Child environment and finite input

`child_process_input_testing <scenario>` provides independent C fixtures for
`env`, `inherit`, `empty-env`, `empty`, `binary`, `early-close`, `timeout`,
`start-error`, `invalid`, `poll`, and `stop`. Run these via `ctest --test-dir build -L process-input
--output-on-failure`. The binary scenario feeds more than 1 MiB (including NUL)
while the child fills both output pipes before consuming stdin. It also destroys
the caller's input buffer after start to check snapshot ownership. Other cases
check EOF, early stdin closure, the original deadline, isolated Unicode environment
and arguments, full replacement versus inheritance, invalid options, and launch
failure. These checks supplement the migrated C process suites and their
independent process-tree observer.

### Streaming process exchange

`child_process_stream_testing <scenario>` / CTest label `process-stream` checks
full binary stdout/stderr through callbacks, including a file sink, alongside
legacy 64-KiB capture/truncation. Output callbacks enable the respective pipe even
when capture is off; no unbounded internal capture is allocated. Input producers
can return WAIT until a READY output is seen, DATA for the next bounded chunk, or
EOF. One absolute deadline covers the entire dialogue. Callback errors terminate
the owned tree and report `CHILD_IO_ERROR`; callbacks must not block or reenter
the same child. File sinks own their files and must report write errors; the caller
must also check flush/close errors. Full delivery is guaranteed for successful
runs, not data the child never wrote before termination. Output order is preserved
within each stream, but no cross-stream ordering is promised.

### Child streaming and interactive exchange

`child_process_stream_testing <scenario>` is registered with the `process-stream`
label. `roundtrip` supplies over 1 MiB of binary stdin in callback-sized chunks
while the child fills both output channels. `large`, `poll`, `capture`, `legacy`
and `file` verify complete independent stdout/stderr byte sequences beyond 64 KiB,
including NUL, callback-only operation, bounded legacy capture and full file
sinks. `dialog` and `dialog-poll` wait for READY before supplying a binary reply,
then deliver EOF. `timeout`, `stop`, `output-error`, `input-error`, `invalid-size`
and `invalid-options` exercise failure and ownership boundaries. Callback/file
errors must fail the child operation rather than silently discarding output.
The migrated C process suites retain independent tree-cleanup observation.

### Scenario contexts

`goat_test_support` is a test-only static library linked to `goat_common`, never
into the interpreter. `test/support/scenario.h` copies the scenario name and
canonical data directory and atomically allocates a fresh work directory beneath
the supplied existing build directory. Build and data directories must be disjoint.
The data path is read-only by contract; the context never changes cwd, writes data
or alters process-global environment. Runners recognize the exact `--keep-work`
flag with `scenario_keep_work_argument` and pass the resulting option.

Call `scenario_finish` exactly once, on every normal success/failure path. It
consumes the context, removes only its owned work directory on success, and
preserves failures or `--keep-work` runs with their absolute path in diagnostics.
Cleanup errors are explicit failures, not successful tests. Identity replacement
is refused; recursive deletion uses the shared filesystem's no-link traversal.
This is cooperative isolation, not protection against malicious concurrent
renames. Tests own any open files and must close them before finishing, especially
on Windows. Abnormal termination can leave a directory behind. Mandatory-check,
process ownership and deadline policies are implemented in `support/execution.h`;
see [the execution contract](support/README.md#execution-and-diagnostics).

`ctest --test-dir build -L scenario-context --output-on-failure` runs the stable
self-tests in Debug and Release, including simultaneous contexts and repeated
runs. No test uses `assert` for required checks or creates results in `test/data`.

## Stable scenario registry and scheduling

`support/registry.h` dispatches exactly one stable ASCII ID per invocation. IDs
must start with a letter and contain only letters, digits, `.`, `_` and `-`;
duplicates, invalid definitions, unknown IDs and extra arguments fail explicitly.
`--list` lists the available IDs. Related assertions remain in one scenario body,
using the existing fail-fast execution/deadline and owned-workspace contract.
A failure does not suppress another CTest registration. Bodies that can crash or
hang run inside a supervised process; CTest timeouts include cleanup margin.

The registry returns 0 for success, 1 for failure and 77 only for an explicit
skip with a nonempty reason printed as `SKIP`. A skip is not a pass or a disabled
test. The registry self-tests include both direct selection and CTest scheduling:

```sh
build/scenario_registry_testing selection build test/data
build/scenario_registry_testing --list
ctest --test-dir build --output-on-failure -R '^framework.registry.selection$'
ctest --test-dir build --output-on-failure --parallel 4 -L scenario-registry
```

`test/ScenarioRegistration.cmake` is the scheduling authority. New standalone
scenarios use `goat_scenario(name labels TIMEOUT seconds COMMAND runner id ...)`;
each name/ID is stable and each registration executes only that scenario.
`LOCKS resource...` declares shared resources via CTest `RESOURCE_LOCK`; all
users of a shared resource must declare the same lock. `PERFORMANCE` adds the
performance label and `RUN_SERIAL`, preventing any concurrent test in the same
CTest invocation. Ordinary scenarios run in parallel; legacy suite registrations
remain serial until individually migrated. Do not use `--stop-on-failure` when
collecting independent results. The nested scheduler acceptance test deliberately
runs a failing scenario and checks that the following one still completes.

Performance isolation is stronger than a CTest lock: locks do **not** coordinate
separate CTest invocations or runner services. Managed GitHub CI uses one job per
GitHub-hosted VM (`ubuntu-24.04`, `ubuntu-24.04-arm`, `windows-latest`); its steps
are sequential, all correctness processes finish before the dedicated performance
step, and no background test invocation is launched. Matrix jobs use distinct VMs.
Performance remains a separate Release step, not part of the parallel correctness
label selection. A self-hosted replacement must provide a dedicated otherwise-idle
machine/VM, one runner service per machine and exclusive scheduling through the
whole performance step; a mere unique runner label is not sufficient. Locally run
`ctest -LE performance --parallel 4` to completion, then `ctest -L performance`
without other competing work. Transitional unavailable tools remain explicit
configure-time disabled registrations rather than silently passing scenarios.

## Loop and index C scenarios

The former Python loop/index runners are now `loop_index_testing`, registered as
`native.for` and `native.index` (label `language-roundtrip`). See the
[scenario correspondence and parity commands](src/functional/README.md).

### Module runtime requirements (#389)

`module_delivery_requirements_scenarios` preserves real compiler/selector audits
and exact declared glibc-floor delivery. Run CTest label
`module-delivery-requirements-migration` in Debug and Release. The Python original
and temporary original/C gate were removed after all 14 remote parity checks
passed. The controlled older-runtime container rebuilds both Goat and the C runner
and verifies the real producer artifact at its exact declared floor. No Python is
needed by this runner. See the
[fixture provenance, old/new map and direct commands](data/module-delivery-requirements/README.md).
This does not migrate the separate glibc/musl delivery checks (#390).

### Standalone SVG validation (#393)

`integration.graph` checks frozen actual Graphviz output and stdout on every
supported host using the direct C validator after the complete original/C parity
gate. `integration.graph_controls`
selects the independent C controls. Both are included in the
`graph-validation-migration` label in Debug and Release, including native musl.
The portable C runner needs neither Graphviz nor Python. `tool.graph` preserves
the original Bash generation commands and optional Graphviz applicability; its
SVG validation no longer depends on Python.

Build `graph_scenarios`, then run:

```sh
ctest --test-dir build --output-on-failure -L graph-validation-migration
build/graph_scenarios integration.graph build test/data/graph test/data/graph --keep-work
GOAT_BINARY="$PWD/build/goat" GRAPH_SCENARIOS_BINARY="$PWD/build/graph_scenarios" bash scripts/check_graph.sh --keep-work
```

Inputs remain read-only. The C runner copies each quartet into its unique build
workspace; failures and `--keep-work` preserve the actual inspected files.
See [fixture provenance and predicate map](data/graph/README.md).
