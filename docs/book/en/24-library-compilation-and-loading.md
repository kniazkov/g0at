# 24. Library Compilation and Loading

[Contents](index.md) · [Русский](../ru/24-library-compilation-and-loading.md) · [Previous chapter](23-native-abi.md) · [Next chapter](25-native-dispatch-and-vm-fallback.md)

Revision 2. Implementation described: [commit 8d1fe86, including `println`](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0).

<a id="section-24-1"></a>

## 24.1. From text to a loadable file

C compilation is a separate operation performed by an external tool. Goat prepares source, starts the compiler, checks the result, and passes the file to the operating-system loader. Linux uses a shared library, `.so`; Windows uses a `.dll`. A library holds code that a process can load while running.

There are two related scenarios. `--save-library` saves a library beside the source without running the Goat program. Native-execution preparation builds a library in a temporary workspace, loads and validates it, then binds it to bytecode. Successfully creating a file does not by itself establish that it is suitable for that bytecode.

Preparation is coordinated by [native_pipeline.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/codegen/native_pipeline.c); saved C and library output is handled by [c_output.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/cli/c_output.c).

<a id="section-24-2"></a>

## 24.2. The compiler and its arguments

The executable comes from a nonempty `CC` environment variable; otherwise Linux uses `cc` and Windows uses `gcc`. `CC` specifies one executable name or path, not a command string with additional flags.

On Linux, [native_compiler_linux.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/codegen/native_compiler_linux.c) calls `posix_spawnp` with an argument array. On Windows, [native_compiler_windows.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/codegen/native_compiler_windows.c) locates the program through `SearchPathA`, constructs a quoted command line, and calls `CreateProcessA`. No command shell is started to interpret the arguments.

| Purpose | Linux | Windows / MinGW |
|---|---|---|
| Language and optimization | `-std=c11 -O2` | `-std=c11 -O2` |
| Library | `-fPIC -shared` | `-shared` |
| Numeric semantics | `-fno-fast-math -ffp-contract=off` | `-fno-fast-math -ffp-contract=off` |
| Unresolved symbols | `-Wl,-z,defs` | `-Wl,--no-undefined` |
| Compiler runtime libraries | Ordinary linking | `-static -static-libgcc` |
| Bitness | Determined by compiler environment | `-m32` or `-m64`, matching the Goat build |
| Exports | Ordinary ELF rules | `-DGOAT_NATIVE_BUILD -Wl,--exclude-all-symbols` |

Both commands also pass `-o`, the output path, the C-source path, and `-lm`. Argument order and path passing are controlled by the implementation, not a user shell.

> [!CAUTION]
> Automatic building is implemented for Linux and Windows with this compiler interface. An arbitrary C compiler with different flags is not automatically supported. No compiler completion timeout is set: a stalled external process can delay preparation.

<a id="section-24-3"></a>

## 24.3. Working files and publication

Each build operation creates a unique directory beside its target file. Linux uses `mkdtemp`; Windows uses a random name and creates a new directory. The directory holds source, the output library, and the compiler log. On Windows, the temporary DLL uses the final base name because that name participates in the PE export format.

Compiler standard input is redirected to `/dev/null` or `NUL`; output and errors go to a log file. This permits waiting without a deadlock caused by a full pipe. At most 64 KiB of diagnostics is read into memory; truncation is reported separately. The exit code is checked, as is signal termination on Linux.

Success requires an existing, nonempty regular output file. Publication uses rename on Linux and `MoveFileExA` with replacement on Windows. Failed compilation must not overwrite a previously saved library. Temporary files are removed; cleanup failure is recorded separately from the compiler result.

For execution, the whole build takes place inside a temporary Goat workspace. After successful loading, responsibility for removing it passes to the library object. The directory is needed until unloading, not merely until the preparation function returns.

<a id="section-24-4"></a>

## 24.4. Why users need not copy libgcc

MinGW can produce a DLL dependent on `libgcc_s_dw2-1.dll`, which may itself require `libwinpthread-1.dll`. Having `gcc.exe` in `PATH` does not automatically make the entire chain available to the selected DLL-loading mode.

The current Windows command uses `-static -static-libgcc` to link GCC runtime and MinGW POSIX threads code into the output. For libraries created by this pipeline, with the corresponding static libraries present in the compiler distribution, manually copying those two DLLs is not a required step.

This does not eliminate every dependency. Windows system DLLs remain system DLLs, and a compiler installation is still required for building. Chapter 26 covers running completed artifacts without a compiler.

<a id="section-24-5"></a>

## 24.5. Loading, validation, and ownership

Linux obtains an absolute path and calls `dlopen` with `RTLD_NOW | RTLD_LOCAL`: symbols are resolved at load time, and the library does not publish them into the global lookup scope. Windows uses a full path and `LoadLibraryExA` with `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`. The loader then looks up exactly `goat_native_query_v1` and validates the ABI from chapter 23.

The native pipeline checks the entire expected inventory before installing bindings. If binding fails, already installed bindings are cleared. A `ready` state requires at least one bound function.

A function descriptor (a structure referring to its specializations) retains the library. Function objects and bytecode can retain descriptors. Releasing the final reference unloads the library and then cleans up its workspace. An adapter pointer must therefore not outlive the machine code containing it. Shared ownership logic is in [native_library.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/model/native_library.c); platform code is in [native_library_linux.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/model/native_library_linux.c) and [native_library_windows.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/model/native_library_windows.c).

> [!CAUTION]
> Loading is not a sandbox (an isolated execution environment). Library initializers may execute before ABI-table validation. The compiler and loaded code must be trusted. Current Windows path operations and system messages use the `A` API variants; full support for all Unicode paths and correct UTF-8 for every localized error is not provided.

<a id="section-24-6"></a>

## 24.6. Commands for both platforms

For chapter 22's working copy, on Linux:

```sh
./goat --save-library build/book-native/module.goat
./goat --native required --print-native build/book-native/module.goat
```

The first command saves `module.so`. The second prepares its own temporary library and runs the program; it does not mean “pick up the previously saved `.so`.”

In PowerShell, from the repository root:

```powershell
New-Item -ItemType Directory -Force .\build\book-native | Out-Null
Copy-Item .\docs\book\examples\22-native-module.goat .\build\book-native\module.goat
.\goat.exe --save-c .\build\book-native\module.goat
.\goat.exe --save-library .\build\book-native\module.goat
.\goat.exe --native required --print-native .\build\book-native\module.goat
```

The saved library is named `module.dll` here. Both platforms should produce `7`, `6`, `4.0`, followed by a report with `preparation=ready`. Compiler-start failure, compilation failure, and loading failure are different stages; an exact status and diagnostic are more useful than merely saying that native code does not work.

External-tool behavior and output preservation are checked by [check_native_library.sh](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/scripts/check_native_library.sh); Windows cases, including linking, by [check_native_windows.sh](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/scripts/check_native_windows.sh); complete preparation by [check_native_pipeline.sh](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/scripts/check_native_pipeline.sh).
