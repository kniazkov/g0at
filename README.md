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

## A taste of Goat

Functions are values. An inner function can retain access to its enclosing scope (a closure):

```text
const add = func(a) {
    return func(b) {
        return a + b;
    };
};
const add_two = add(2);
println(add_two(3));
```

Output: `5`, followed by a newline. See the [book examples](docs/book/examples) and [functional tests](test/functional) for more programs.

Goat supports dynamically typed values, lexical scopes, functions and recursion, conditional branches, block-based objects, exceptions, and mathematical and input/output built-ins. Static analysis tracks approximate values, function calls, effects, and numeric specializations. Eligible functions can be translated to C and executed as machine code through a shared library (native execution).

> [!CAUTION]
> Goat is an experimental implementation. There is no loop syntax, array indexing, property-access syntax, or module import syntax. Native generation covers a restricted numeric subset; the VM executes other supported language constructs. The book marks implementation limitations with red callouts.

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

The Debug build script copies `goat` to the repository root and runs the unit, analysis, and runtime functional suites. The example prints `it works!`.

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

The Debug script builds the interpreter and runs the same three suites. For a Release build without tests:

```powershell
.\scripts\build_release_mingw.cmd
```

It writes `build\release\goat.exe` and copies it to the repository root.

### Native execution and saved programs

Ordinary source execution uses bytecode and needs no C compiler at runtime. To try native execution and inspect its report:

```bash
./goat --native required --print-native docs/book/examples/22-native-module.goat
```

In PowerShell, replace `./goat` with `.\goat.exe`. Native preparation requires a compatible C compiler: `CC` selects one executable name or path, defaulting to `cc` on Linux and `gcc` on Windows. Newly generated MinGW libraries link compiler support libraries statically, so users do not need to copy `libgcc_s_*.dll` or `libwinpthread-1.dll` beside Goat; the compiler installation must provide the static archives.

`--native required` requires at least one bound native function. It does not require every call to execute natively. `--native auto` allows bytecode execution when preparation fails. Both modes require optimization, which is enabled by default.

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

Adding `--native required` to the compile command also builds a companion `.so` or `.dll`. Keep that library beside the `.gbin` with the same basename. Execution of the saved pair needs neither the source nor a C compiler. See [Separate Compilation](docs/book/en/26-separate-compilation.md) for compatibility and failure rules.

## Inspect and test

`--print-source-code`, `--print-bytecode`, and `--print-analysis` expose intermediate representations; these options also execute the program. `--save-graph graph.svg` writes an AST image and requires Graphviz (`dot` on `PATH`). `--print-c` exports generated C without executing the program. See [Observability](docs/book/en/27-observability.md) for complete examples.

The build scripts run the three core suites. Additional scripts check generated C, the native ABI, loading, dispatch, saved programs, and performance. CI runs Linux GCC/Clang and Windows MINGW32/MINGW64/UCRT64 configurations. The [testing chapter](docs/book/en/28-testing.md) explains the suites, fixture formats, and CI checks. The [benchmark instructions](test/performance/README.md) describe how to run the Release performance check.

## Author and license

Created by [Ivan Kniazkov](https://github.com/kniazkov).

Goat is distributed under the [MIT license](LICENSE.txt).
