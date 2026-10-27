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
