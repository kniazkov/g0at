# The Goat Programming Language

[![Build and test](https://github.com/kniazkov/g0at/actions/workflows/build_and_test.yml/badge.svg)](https://github.com/kniazkov/g0at/actions/workflows/build_and_test.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE.txt)

[Русская версия](README.ru.md)

**Goat is an experimental programming language implemented in C, with a bytecode virtual machine, static analysis, and a native backend.**

Created by Ivan Kniazkov, the project explores language implementation and provides working examples for technical discussions and consultations. Its source code makes the route from program text to execution available for inspection.

## Documentation

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

Output: `5`, followed by a newline. See the [book examples](docs/book/examples) and [functional tests](test/functional) for more programs.

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

Clone the repository and enter its directory:

```sh
git clone https://github.com/kniazkov/g0at.git
cd g0at
```

### Linux

Install GCC or Clang, GNU Make, Bash, CMake 3.13 or newer, and the standard C development libraries, including pthread and libm.

```bash
bash scripts/build.sh
./goat example/hello_world.goat
```

The Debug script builds `goat`, `goat-inspect`, `goat-merge` and component-test executables in `build`, copies only `goat` to the repository root, and runs the strict module-JSON checks plus the unit, analysis and runtime functional suites. It does not run the complete CI integration matrix. The example prints `it works!`.

Compilation runs in parallel, using the logical CPU count by default. Pass a positive job count to limit it (`1` builds sequentially):

```bash
bash scripts/build.sh 8
```

Both Debug scripts also accept `CMAKE_BUILD_PARALLEL_LEVEL` as the default job count; an explicit argument takes precedence. Tests still run sequentially. Use `--help` for usage.

To use Clang in a separate build directory:

```bash
CC=clang BUILD_DIR=build/clang bash scripts/build.sh
```

For a Release build without running tests:

```bash
bash scripts/build_release.sh
```

The Release executable is placed in `build/release` and copied to the repository root. `CC` and `BUILD_DIR` also apply to this script. Release builds disable the project's allocation accounting and guards (`MEMORY_DEBUG`); Debug builds retain them.

### Windows (MinGW)

Install CMake 3.13 or newer and a MinGW toolchain with GCC, `mingw32-make`, and pthread support available on `PATH`. In PowerShell, from the repository root:

```powershell
.\scripts\build_mingw.cmd
.\goat.exe .\example\hello_world.goat
```

The Debug script builds the interpreter, both module tools and component-test executables in `build`, copies only `goat.exe` to the root, and runs the same JSON checks and three core suites. To choose the parallel job count:

```powershell
.\scripts\build_mingw.cmd 8
```

Without an argument it uses `CMAKE_BUILD_PARALLEL_LEVEL`, then `%NUMBER_OF_PROCESSORS%`. For a Release build without tests:

```powershell
.\scripts\build_release_mingw.cmd
```

It writes `build\release\goat.exe` and copies it to the repository root.

The Release scripts also create a relocatable archive in `<build directory>/packages`
(`.tar.gz` on Linux, `.zip` on Windows). It contains `bin/goat`, `bin/goat-inspect`, `bin/goat-merge` (with `.exe` on Windows), public native
headers, static support library, examples, license and [usage instructions](docs/package/README.md).
CI tests the extracted archive and publishes it as a `goat-*` artifact. To check
an archive locally (Python 3 and a matching C compiler are needed for testing):

```sh
python3 scripts/check_package.py build/release/packages
```

The test relocates the package outside the checkout, compiles using only its
public headers, exports C, builds native modules, and runs saved VM/native
programs after removing their source, with the compiler unavailable.


### Native execution and saved programs

A standalone `.goat` uses `--native off` by default and needs no C compiler for VM execution. Ready binary execution defaults to `auto`; `.gmod` uses its descriptor’s mode (default `auto`) and target (default `host`). Use explicit `--native off` for compiler-independent module builds/runs. To try native execution and inspect its report:

```bash
./goat --native required --print-native docs/book/examples/22-native-module.goat
```

In PowerShell, replace `./goat` with `.\goat.exe`. Native preparation needs a compatible C compiler. `--cc` selects one executable name or path, without shell flags; standalone source commands also honor `CC`, overridden by `--cc`. Module builds probe supported PATH toolchains; `--list-native-targets` reports actual successful targets, not merely recognized names. An explicit unavailable target fails rather than silently changing to host. See [native build rules](docs/module-build-api.md).

For standalone source, `required` needs at least one bound native function and `auto` allows supported preparation failures to use VM. Native preparation needs analysis (`--optimize all`, the default). Modules retain mandatory interface analysis even with `--optimize none`; `required` checks a compatible specialization of the root’s own files, not merely its dependencies. **Neither mode requires every call to be native.** Applicable module libraries load lazily; corrupt artifacts and detected ABI violations do not permit silent fallback or replay. See [native loading](docs/module-loading-api.md).

To save bytecode without executing the program, then run it:

```bash
mkdir -p build/quickstart
cp docs/book/examples/22-native-module.goat build/quickstart/program.goat
./goat --compile build/quickstart/program.goat
./goat --run build/quickstart/program.gbin
```

PowerShell equivalent:

```powershell
New-Item -ItemType Directory -Force .\build\quickstart | Out-Null
Copy-Item .\docs\book\examples\22-native-module.goat .\build\quickstart\program.goat
.\goat.exe --compile .\build\quickstart\program.goat
.\goat.exe --run .\build\quickstart\program.gbin
```

This example has no include/import and uses the standalone binary path. Adding `--native required` to its compile command also builds a companion `.so` or `.dll`. Keep that library beside the `.gbin` with the same basename. Execution of the saved pair needs neither the source nor a C compiler. See [Separate Compilation](docs/book/en/26-separate-compilation.md) for compatibility and failure rules.

Generated native C uses the [native support library](docs/native-runtime.md). `--save-c` exports its headers and `libgoat_native.a` beside the C file; automatic DLL/SO compilation stages and links the matching archive from the interpreter bundle.

The [model layout](docs/model-layout.md) describes the separation of value storage, prototypes, and built-in method adapters (English and Russian).

### Modules: build, run, inspect and merge

Module-system v1 is implemented. A strict JSON `.gmod` identifies a name, exact version and main source. `include` joins source files with separate scopes; `import` uses a built module with an exact dependency version. Unmodified declarations are file-local, `public` is module-local, and `external` crosses module boundaries; `protected`/`private` retain their object-domain rules. A caller does not transfer its privileges to the called function. See the [tutorial and format](docs/book/en/30-modules.md).

A module build publishes a single deterministic ZIP `.gbin` containing metadata, portable bytecode and optional native variants. It does not execute initializers or load DLL/SO. Running `.gmod` builds in memory without publishing; running a ready ZIP needs no source or compiler, but used dependency archives must be discoverable. ZIP modules do **not** use the standalone companion-library layout above. The old host-dependent `GOATBI11` format is not accepted: rebuild old artifacts.

Try the checked-in two-module tutorial in a working copy. After the default Debug build, from the repository root:

```bash
export PATH="$PWD/build:$PATH"
mkdir -p build/quickstart/modules
cp -R docs/book/examples/30-modules/. build/quickstart/modules/
cd build/quickstart/modules
```

PowerShell setup (from the repository root):

```powershell
$env:PATH = "$PWD\build;$env:PATH"
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

Return to the repository root before the commands below (`cd ../../..`, or `Set-Location ../../..`). For a custom build directory, use that directory in PATH instead; release packages provide all three tools in `bin`.

## Inspect and test

For standalone sources, `--print-source-code`, `--print-bytecode`, and `--print-analysis` expose intermediate representations; these options also execute the program. `--save-graph graph.svg` writes an AST image and requires Graphviz (`dot` on `PATH`). `--print-c` exports generated C without executing the program. Module input currently rejects C/source/analysis/graph exports explicitly; use `goat-inspect` for module metadata and graph diagnostics. See [Observability](docs/book/en/27-observability.md) for complete examples.

The Debug scripts run strict module-JSON checks and three core suites; the full CI additionally runs component/integration suites for formats, module graphs, access, initialization, native ABI/build/loading, threads, exit, inspect/merge, delivery and the bilingual tutorial. The seven platform jobs cover Linux GCC/Clang on x86-64 and AArch64, and Windows MINGW32/MINGW64/UCRT64. Style, Doxygen and format vectors have separate checks. See the [testing chapter](docs/book/en/28-testing.md) and [workflow](.github/workflows/build_and_test.yml).

For the focused book/README tutorial check after building (Python 3 required):

```sh
python3 scripts/check_module_book.py build/goat
```

On Windows use `python scripts/check_module_book.py build/goat.exe`. This checks the documented examples and local links, not every possible program or the semantic equivalence of all prose. See [benchmarks](test/performance/README.md) for Release performance checks.

Portability claims are bounded by evidence: current CI executes relocated native/VM programs, genuine mixed Windows CRT variants and control modules on glibc 2.35/2.36. Ordinary declared glibc 2.14/2.17 floors and actual Windows 10 execution are **not** yet demonstrated on those exact systems. The [delivery evidence and validation plan](docs/module-delivery-tests.md) separates these limits from implemented functionality. Native ELF/PE compatibility metadata is not a guarantee about every host or the interpreter executable itself.

## More language guides

Prototype-based object construction and multiple inheritance are described in [the `->` operator guide](docs/prototype-inheritance.md) (English and Russian).

See [loops and loop transfers](docs/loops.md) for syntax, scopes, and native support (English and Russian).

See [compound assignments](docs/compound-assignment.md) for evaluation order, exception behavior, and native support (English and Russian).

See [switch selection](docs/switch.md) for ordered variants, branch scopes, diagnostics, and native support (English and Russian).

## Author and license

Created by [Ivan Kniazkov](https://github.com/kniazkov).

Goat is distributed under the [MIT license](LICENSE.txt).
