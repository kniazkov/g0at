# The Goat Programming Language

[![Build and test](https://github.com/kniazkov/g0at/actions/workflows/build_and_test.yml/badge.svg)](https://github.com/kniazkov/g0at/actions/workflows/build_and_test.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE.txt)

[Русская версия](README.ru.md)

**Goat is an experimental programming language implemented in C, with a bytecode virtual machine, static analysis, and a native backend.**

Created by Ivan Kniazkov, the project explores language implementation and provides working examples for technical discussions and consultations. Its source code makes the route from program text to execution available for inspection.

## Documentation

[Implementation reading guides](docs/internals/en/index.md): current source-level
walkthroughs with Doxygen links and examples taken from the analysis tests.

**[Goat Internals — English](docs/book/en/index.md)** · **[Goat изнутри — русский](docs/book/ru/index.md)**

The book describes the implemented system in both languages, with the same structure and content. It covers scanning and parsing, bytecode, the runtime, static analysis, native code generation, testing, and performance measurement. Examples accompany the explanations; source references identify the implementation revision described.

| Topic | English | Русский |
| --- | --- | --- |
| Language | [Reference](docs/book/en/appendix-b-language.md) | [Справочник](docs/book/ru/appendix-b-language.md) |
| Built-in functions | [Reference](docs/book/en/appendix-d-builtins.md) | [Справочник](docs/book/ru/appendix-d-builtins.md) |
| Command-line options | [Launch interfaces](docs/book/en/appendix-e-launch-interfaces.md) | [Интерфейсы запуска](docs/book/ru/appendix-e-launch-interfaces.md) |
| Source layout | [Implementation map](docs/book/en/appendix-f-implementation-map.md) | [Карта реализации](docs/book/ru/appendix-f-implementation-map.md) |
| Current limitations | [Implementation boundaries](docs/book/en/appendix-g-implementation-boundaries.md) | [Границы реализации](docs/book/ru/appendix-g-implementation-boundaries.md) |
| Modules and access | [Runnable tutorial](docs/book/en/30-modules.md) · [Access rules](docs/book/en/appendix-i-access-control.md) | [Исполняемый пример](docs/book/ru/30-modules.md) · [Правила доступа](docs/book/ru/appendix-i-access-control.md) |

## A taste of Goat

Functions are values. An inner function can retain access to its enclosing scope (a closure):

<!-- readme-closure -->
```goat
const add = func(a) {
    return func(b) {
        return a + b;
    };
};
const add_two = add(2);
println(add_two(3));
```

Output: `5`, followed by a newline. See the [book examples](docs/book/examples) and [functional tests](test/data/functional) for more programs.

A C-style loop uses the same values and scopes:

<!-- readme-loop -->
```goat
var sum = 0;
for (var i = 0; i < 5; i++) sum = sum + i;
println(sum);
```

This prints `10`. Header slots can be empty; the body may be a single statement or a block. [Chapter 2](docs/book/en/02-implemented-language.md) describes scope and control flow; [chapter 22](docs/book/en/22-c-code-generation.md) shows a native numeric loop.

Goat supports dynamically typed values, lexical scopes, functions and recursion, conditional branches and `switch`, `for`, `while`, and `do…while` loops with `break`/`continue`, block-based objects, exceptions, arrays, prototype inheritance, include/import, file/module access modifiers, program threads, and mathematical and input/output built-ins. Static analysis tracks approximate values, function calls, effects, and scalar specializations. Eligible functions can be translated to C and executed as machine code through a shared library (native execution).

> [!CAUTION]
> Goat is an experimental implementation. Loops, indexed/property access and the module system are implemented; [the module tutorial](docs/book/en/30-modules.md) exercises build, run, inspect and merge. Native generation covers only proven eligible specializations, not every language construct; the VM remains the fallback. The book marks actual implementation and [portability limits](docs/module-delivery-tests.md), rather than treating unexecuted lower-bound OS tests as passed.

## Build and run

For an extended local CI run (Debug + Release):

```sh
bash scripts/build.sh --full-check 4
```

On Windows, with MinGW and MSYS2 Bash in PATH:

```bat
scripts\build_mingw.cmd --full-check 4
```

This includes integration/native tests, a Release package, allocation-failure builds
and standalone generators. It uses the current compiler/architecture, not the entire
GitHub matrix. See [scope, prerequisites and reports](test/full-check.md).
The existing quick mode remains the default.


Clone the repository and enter its directory:

```sh
git clone https://github.com/kniazkov/g0at.git
cd g0at
```

The CMake entry point is the repository root, not `src/`. For a clean direct build:

```bash
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel 2
```

From another working directory, pass an absolute repository path to `-S` and an absolute build path to `-B`. Build scripts locate the repository relative to themselves. Existing build directories configured with `-S src` cannot be reused: choose a fresh build directory (or remove only the old generated build directory). Executables remain directly in the selected build directory; Release packages remain in its `packages/` subdirectory. The build scripts additionally publish all three tools to the repository’s `target/` directory. A direct CMake build only populates its build directory; publish it with `cmake -DGOAT_BUILD_DIR=build/manual -P scripts/publish_tools.cmake` if needed.

The scripts create `target/` automatically and refresh its tools after a successful build. Copy failures are fatal. `--test-only` uses the build directory without republishing; `--full-check` publishes Release tools only after all checks succeed. Paths are relative to the repository, including when a script is called from another directory. Keep custom `BUILD_DIR` and `FULL_CHECK_DIR` paths under `build/` or outside the checkout so generated files stay out of Git. On Windows, Debug executables still use the active MinGW runtime from PATH; Release packages remain relocatable.

### Linux

Install GCC or Clang, GNU Make, Bash, CMake 3.13 or newer, and the standard C development libraries, including pthread and libm. Linux native backends support glibc and musl on x86-64 and AArch64; build Goat and its native modules for the same runtime family. Installing a second libc does not change the runtime of an already-built Goat process. On Alpine, install `build-base`, `cmake`, `bash` and `linux-headers` before using the same build commands. Ordinary glibc users do not need musl installed.

```bash
bash scripts/build.sh
./target/goat example/hello_world.goat
```

The Debug script builds `goat`, `goat-inspect`, `goat-merge` and component-test executables in `build`, copies all three tools to `target/`, and runs the strict module-JSON checks plus the unit, analysis and runtime functional suites. It does not run the complete CI integration matrix. The example prints `it works!`.

Compilation runs in parallel, using the logical CPU count by default. Pass a positive job count to limit it (`1` builds sequentially):

```bash
bash scripts/build.sh 8
```

Both Debug scripts also accept `CMAKE_BUILD_PARALLEL_LEVEL` as the default job count; an explicit argument takes precedence. Tests still run sequentially. Use `--help` for usage.

Debug, Release and full-check build stages keep each job's output together with
GNU Make 4.0+ (`--output-sync=target`). Messages can appear when a job finishes;
warnings and errors are not filtered. Parallelism and job-count selection stay
unchanged (Release uses `CMAKE_BUILD_PARALLEL_LEVEL` when set). The scripts inspect
the configured generator: Ninja keeps its native output buffering and receives no
Make-specific options. Older/non-GNU Make builds still work, with a warning that
output synchronization is unavailable. For the same behavior in a direct build,
use `cmake -P scripts/build_command.cmake build/manual --parallel 2`.

To measure compilation and tests separately, run `bash scripts/build.sh --build-only 8`, then `bash scripts/build.sh --test-only`. Keep the same `BUILD_DIR` for both commands. The test-only mode uses the existing Debug binaries without rebuilding; omitting the mode still builds and tests in one command.

To use Clang in a separate build directory:

```bash
CC=clang BUILD_DIR=build/clang bash scripts/build.sh
```

For a Release build without running tests:

```bash
bash scripts/build_release.sh
```

The Release executables are built in `build/release` and published to `target/`. `CC` and `BUILD_DIR` also apply to this script. Release builds disable the project's allocation accounting and guards (`MEMORY_DEBUG`); Debug builds retain them.

### Windows (MinGW)

Install CMake 3.13 or newer and a MinGW toolchain with GCC, `mingw32-make`, and pthread support available on `PATH`. In PowerShell, from the repository root:

```powershell
.\scripts\build_mingw.cmd
.\target\goat.exe .\example\hello_world.goat
```

The Debug script builds the interpreter, both module tools and component-test executables in `build`, copies `goat.exe`, `goat-inspect.exe` and `goat-merge.exe` to `target/`, and runs the same JSON checks and three core suites. To choose the parallel job count:

```powershell
.\scripts\build_mingw.cmd 8
```

The same phase options are available on Windows: `.\scripts\build_mingw.cmd --build-only 8`, then `.\scripts\build_mingw.cmd --test-only`.

Without an argument it uses `CMAKE_BUILD_PARALLEL_LEVEL`, then `%NUMBER_OF_PROCESSORS%`. For a Release build without tests:

```powershell
.\scripts\build_release_mingw.cmd
```

It builds in `build\release` and publishes `target\goat.exe`, `target\goat-inspect.exe` and `target\goat-merge.exe`.

The Release scripts also create a relocatable archive in `<build directory>/packages`
(`.tar.gz` on Linux, `.zip` on Windows). It contains `bin/goat`, `bin/goat-inspect`, `bin/goat-merge` (with `.exe` on Windows), public native
headers, static support library, examples, license and [usage instructions](docs/package/README.md).
CI tests the extracted archive and publishes it as a `goat-*` artifact. To check
an archive locally, build the package and C test runner, then reconfigure so
CTest sees the archive (a matching C compiler and CMake are required):

```sh
cmake --build build/release --target package package_scenarios
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
ctest --test-dir build/release -R '^package[.]relocated$' --output-on-failure
```

See [package verification](test/data/package/README.md) for direct invocation.

The test relocates the package outside the checkout, compiles using only its
public headers, exports C, builds native modules, and runs saved VM/native
programs after removing their source, with the compiler unavailable.


#### MSYS2 Make: `--sync-mutex` error

If configuration succeeds but the build stops with:

```text
mingw32-make: the '--sync-mutex' option requires a non-empty string argument
```

check the build tools before changing Goat sources. The MSYS2 package
`mingw-w64-x86_64-make 4.4.1-4` has a [reported regression](https://github.com/msys2/MINGW-packages/issues/29218)
with output synchronization and recursive parallel builds. Goat's build wrapper
enables `--output-sync=target`, which can trigger this failure. A successful CI
build does not guarantee that a local machine has the same package revision.

In PowerShell, check the executable and the package revision (adjust the MSYS2
installation path if necessary):

```powershell
where.exe mingw32-make
mingw32-make --version
C:\msys64\usr\bin\pacman.exe -Q mingw-w64-x86_64-make
```

`make --version` alone shows `4.4.1`, not the MSYS2 package revision `-4`.
Stop builds and close other MSYS2 sessions, then open `C:\msys64\msys2.exe`
and perform a [full MSYS2 update](https://www.msys2.org/docs/updating/):

```bash
pacman -Syu
```

If prompted to close the terminal during a core update, agree, reopen it and
run `pacman -Syu` again. Repeat until no updates remain. Then retry
`.\scripts\build_mingw.cmd` from PowerShell. Updating MSYS2 resolved the
reported local failure; no particular fixed package revision is assumed here.

To isolate the output synchronization issue, build one target directly:

```powershell
cmake --build build --parallel 4 --target process_exit_delayed_testing -- --output-sync=none
```

This bypasses the wrapper's output synchronization while retaining parallelism.
It builds only that target and its dependencies, not the full project, and does
not run tests or publish executables to `target/`. If updating does not help,
include the package revision and the results of this command in the bug report.

### Native execution and saved programs

A standalone `.goat` uses `--native off` by default and needs no C compiler for VM execution. Ready binary execution defaults to `auto`; `.gmod` uses its descriptor’s mode (default `auto`) and target (default `host`). Use explicit `--native off` for compiler-independent module builds/runs. To try native execution and inspect its report:

```bash
./target/goat --native required --print-native docs/book/examples/22-native-module.goat
```

In PowerShell, replace `./target/goat` with `.\target\goat.exe`. Native preparation needs a compatible C compiler. `--cc` selects one executable name or path, without shell flags; standalone source commands also honor `CC`, overridden by `--cc`. Module builds probe supported PATH toolchains; `--list-native-targets` reports actual successful targets, not merely recognized names. An explicit unavailable target fails rather than silently changing to host. See [native build rules](docs/module-build-api.md).

For standalone source, `required` needs at least one bound native function and `auto` allows supported preparation failures to use VM. Native preparation needs analysis (`--optimize all`, the default). Modules retain mandatory interface analysis even with `--optimize none`; `required` checks a compatible specialization of the root’s own files, not merely its dependencies. **Neither mode requires every call to be native.** Applicable module libraries load lazily; corrupt artifacts and detected ABI violations do not permit silent fallback or replay. See [native loading](docs/module-loading-api.md).

To save bytecode without executing the program, then run it:

```bash
mkdir -p build/quickstart
cp docs/book/examples/22-native-module.goat build/quickstart/program.goat
./target/goat --compile build/quickstart/program.goat
./target/goat --run build/quickstart/program.gbin
```

PowerShell equivalent:

```powershell
New-Item -ItemType Directory -Force .\build\quickstart | Out-Null
Copy-Item .\docs\book\examples\22-native-module.goat .\build\quickstart\program.goat
.\target\goat.exe --compile .\build\quickstart\program.goat
.\target\goat.exe --run .\build\quickstart\program.gbin
```

`--compile` always produces a ZIP module beside the source. Its name is the source basename without the extension and its version is `0`. Invalid module names require renaming the file or supplying a `.gmod`; names are never silently rewritten. Optional native `.so`/`.dll` variants live inside the ZIP. Running it needs neither source nor a C compiler. `goat-inspect`, `goat-merge` and imports accept the same artifact. Direct-source imports must specify an exact version; `include` files are embedded and imported modules remain external dependencies.

Generated native C uses the [native support library](docs/native-runtime.md). `--save-c` exports its headers and `libgoat_native.a` beside the C file; automatic DLL/SO compilation stages and links the matching archive from the interpreter bundle.

The [model layout](docs/model-layout.md) describes the separation of value storage, prototypes, and built-in method adapters (English and Russian).

### Modules: build, run, inspect and merge

Module-system v1 is implemented. A strict JSON `.gmod` identifies a name, exact version and main source. `include` joins source files with separate scopes; `import` uses a built module with an exact dependency version. Unmodified declarations are file-local, `public` is module-local, and `external` crosses module boundaries; `protected`/`private` retain their object-domain rules. A caller does not transfer its privileges to the called function. See the [tutorial and format](docs/book/en/30-modules.md).

A module build publishes a single deterministic ZIP `.gbin` containing metadata, portable bytecode and optional native variants. It does not execute initializers or load DLL/SO. Running `.gmod` builds in memory without publishing; running a ready ZIP needs no source or compiler, but used dependency archives must be discoverable. Direct source execution uses the same compiler result without a ZIP round trip. The REPL retains its separate persistent session. Old standalone envelopes (including `GOATPG01`) and raw bytecode are not launchable: recompile from source.

Try the checked-in two-module tutorial in a working copy. After the default Debug build, from the repository root:

```bash
export PATH="$PWD/target:$PATH"
mkdir -p build/quickstart/modules
cp -R docs/book/examples/30-modules/. build/quickstart/modules/
cd build/quickstart/modules
```

PowerShell setup (from the repository root):

```powershell
$env:PATH = "$PWD\target;$env:PATH"
New-Item -ItemType Directory -Force .\build\quickstart\modules | Out-Null
Copy-Item .\docs\book\examples\30-modules\* .\build\quickstart\modules\ -Recurse -Force
Set-Location .\build\quickstart\modules
```

Then run these same commands on either platform:

<!-- module-book-cli -->
```sh
goat build geometry.gmod --native off
goat build app.gmod --native off
goat run app.gmod --native off
goat run modules/tutorial/1.0.gbin --module-dir modules --native off
goat-inspect --json --module-dir modules modules/tutorial/1.0.gbin
goat-merge merged.gbin modules/geometry/1.0.gbin
```

Both run commands print `25`. `--module-dir modules` explicitly locates dependencies; search does not infer the repository root from the archive’s nested path. The merge command accepts one source here; multiple sources add compatible native variants of the same main build. Conflicting scalar informational fields are rejected. Multiple nonempty author lists require an interactive ordering choice; without the required input, merging fails without replacing the destination.

`goat-inspect` validates the full dependency graph and reports metadata; **compatible local native libraries may also be probed in isolated subprocesses, executing constructors**. `--target PROFILE.json` selects the environment profile; a foreign profile is analyzed without local loading, but a profile matching the current environment still permits probes. Process isolation is not a security sandbox for malicious native code. `goat-merge` never executes bytecode or loads libraries and does not require external dependency archives. See [inspect](docs/module-inspect-api.md) and [merge](docs/module-merge-api.md).

Return to the repository root before the commands below (`cd ../../..`, or `Set-Location ../../..`). The scripts publish to the same `target/` even with a custom build directory; release packages provide all three tools in `bin`.

## Inspect and test

For standalone sources, `--print-source-code`, `--print-bytecode`, and `--print-analysis` expose intermediate representations; these options also execute the program. `--save-graph graph.svg` writes an AST image and requires Graphviz (`dot` on `PATH`). `--print-c` exports generated C without executing the program. Module input currently rejects C/source/analysis/graph exports explicitly; use `goat-inspect` for module metadata and graph diagnostics. See [Observability](docs/book/en/27-observability.md) for complete examples.

The Debug scripts run strict module-JSON checks and three core suites; the full CI additionally runs component/integration suites for formats, module graphs, access, initialization, native ABI/build/loading, threads, exit, inspect/merge, delivery and the bilingual tutorial. The original seven platform jobs cover Linux/glibc GCC/Clang on x86-64 and AArch64, and Windows MINGW32/MINGW64/UCRT64. Two additional native Alpine jobs cover Linux/musl x86-64 and AArch64, including relocated Release packages and cross-libc module selection. Style, Doxygen and format vectors have separate checks. See the [testing chapter](docs/book/en/28-testing.md) and [workflow](.github/workflows/build_and_test.yml).

For the focused book/README tutorial check after building:

```sh
cmake --build build --target module_book_scenarios
ctest --test-dir build --output-on-failure -L module-book-migration
```

The same commands work on Windows; this check does not require Python. This checks the documented examples and local links, not every possible program or the semantic equivalence of all prose. See [benchmarks](test/data/performance/README.md) for Release performance checks.

Portability claims are bounded by evidence: current CI executes relocated native/VM programs, genuine mixed Windows CRT variants, control modules on glibc 2.35/2.36, and musl modules on their linked runtime version. The musl version floor is conservative; execution on an older musl is not claimed. Ordinary declared glibc 2.14/2.17 floors and actual Windows 10 execution are **not** yet demonstrated on those exact systems. The [delivery evidence and validation plan](docs/module-delivery-tests.md) separates these limits from implemented functionality. Native ELF/PE compatibility metadata is not a guarantee about every host or the interpreter executable itself.

## More language guides

Prototype-based object construction and multiple inheritance are described in [the `->` operator guide](docs/prototype-inheritance.md) (English and Russian).

See [loops and loop transfers](docs/loops.md) for syntax, scopes, and native support (English and Russian).

See [compound assignments](docs/compound-assignment.md) for evaluation order, exception behavior, and native support (English and Russian).

See [switch selection](docs/switch.md) for ordered variants, branch scopes, diagnostics, and native support (English and Russian).

## Author and license

Created by [Ivan Kniazkov](https://github.com/kniazkov).

Goat is distributed under the [MIT license](LICENSE.txt).

## CTest suites

Build all targets, then use `ctest --test-dir build --output-on-failure`. Use `ctest --test-dir build -N` for the inventory and `-L integration` to select the existing integration runners. Bash remains required for shell-based suites; build, generators, tests and packaging do not require Python. See [test prerequisites, labels and platform coverage](test/README.md).

Project policy: new build tooling, generators, tests and packaging must not introduce a Python dependency; enforce this during development and review.
