# Running the transitional test suites

Configure from the repository root, build all targets, then run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build -N
ctest --test-dir build --output-on-failure
```

CTest registration is in `test/CTestLists.cmake`. Existing runners and their
command-line interfaces are unchanged. Every monolithic runner is one stable
CTest test for now; later migration steps split it into individual scenarios.
The 59 `integration.*` tests replace exactly the former Debug book, child-process,
ZIP/module/native-metadata, JSON and cleanup commands on Linux and Windows.
CI still runs its specialized native, sanitizer, fault, pattern-initialization,
Release/package and controlled glibc/musl container checks unchanged.

Labels select `core`, `integration`, `native`, `format`, `sanitizer`, `fault`,
`package`, `performance`, `style`, `docs` and `graph`. For example:

```sh
ctest --test-dir build --output-on-failure -L integration
ctest --test-dir build --output-on-failure -LE performance
ctest --test-dir build --output-on-failure -L performance
```

Legacy runners share some resources, so registrations use `RUN_SERIAL` and a
bounded timeout. Performance is run separately in CI and must not compete with
other jobs on the same machine. This is not the later parallel scenario framework.
Functional inputs are copied under the selected build directory; failed copies
are retained with their path in the output. Inputs under `test/data` stay read-only.

Python 3 and Bash are transitional test dependencies, not ordinary compiler build
dependencies. Missing tools produce explicitly disabled tests and configure
messages; CI uses `GOAT_TEST_REQUIRE_PYTHON=ON` to prohibit silently missing its
integration coverage. On Windows use the MSYS2 environment and configure
`Python3_EXECUTABLE` to the native Python interpreter, not an MSYS Python.
CTest supplies the configured C compiler and absolute target paths.

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

### Child environment and finite input

`child_process_input_testing <scenario>` provides independent C fixtures for
`env`, `inherit`, `empty-env`, `empty`, `binary`, `early-close`, `timeout`,
`start-error`, `invalid`, `poll`, and `stop`. Run these via `ctest --test-dir build -L process-input
--output-on-failure`. The binary scenario feeds more than 1 MiB (including NUL)
while the child fills both output pipes before consuming stdin. It also destroys
the caller's input buffer after start to check snapshot ownership. Other cases
check EOF, early stdin closure, the original deadline, isolated Unicode environment
and arguments, full replacement versus inheritance, invalid options, and launch
failure. These checks supplement rather than replace the existing Python process
suite and its independent process-tree observer.

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
The existing Python process suite retains its independent tree-cleanup observer.

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
