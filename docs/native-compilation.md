# Native library compilation

`goat --save-library program.goat` analyzes and emits the same dependency-closed
numeric module as `--print-c`, compiles it, and writes `program.so` beside the
input on Linux, or `program.dll` on Windows. It does not execute the Goat program or load the library. Empty inventories
produce valid libraries with empty ABI descriptors. Unsupported specializations
remain omitted; compilation success does not mean the whole program is native.

The option requires `--optimize all`, permits `--save-c`, `--print-c`, analysis-file
and graph output, and rejects other `--print` modes. Without `--save-c`, the temporary
C file is removed. Explicit source/analysis exports are independent artifacts and
may remain if the subsequent library build fails. An input already named with the target library extension
is rejected to avoid overwriting it. Other platforms still return an explicit
unsupported-platform failure.

The compiler uses one header, `src/codegen/native_compiler.h`. The common
`native_compiler.c` owns diagnostics and result cleanup; `native_compiler_linux.c`
and `native_compiler_windows.c` implement the same entry point on their platforms.

## Compiler invocation

`CC` selects one executable name or path; unset or empty uses `cc` on Linux and `gcc` on Windows. It must accept
GCC/Clang-style C and platform linker options. Windows support targets MinGW GCC
(MINGW32, MINGW64 and UCRT64), not the MSVC command-line interface. Names without slashes are searched in
`PATH`. Paths with spaces are supported. No shell is involved: `CC="cc -O3"` is an
executable name, not a command line. Use a trusted executable wrapper for a compiler
launcher. The project and its ordinary interpreter do not require a C++ compiler.

The Linux argument vector is:

```
cc -std=c11 -O2 -fPIC -shared -fno-fast-math -ffp-contract=off -Wl,-z,defs \
   -o PRIVATE/module.so PRIVATE/module.c -lm
```

`-fno-fast-math` preserves the numeric contract, `-ffp-contract=off` prevents fused
expressions changing rounding, and `-z defs` rejects unresolved symbols during
linking. Generated C also checks binary64 and rejects fast math. `CFLAGS` and
`LDFLAGS` are not appended implicitly. The selected compiler/wrapper and its inherited
environment are trusted build configuration, not a sandbox or cross-compilation
interface. Third-party archives are not linked automatically by this source-export
path; the public ABI supports separately built providers.

## Windows compilation and exports

Windows uses `-shared -DGOAT_NATIVE_BUILD -m32` or `-m64` matching the interpreter,
`-fno-fast-math -ffp-contract=off -Wl,--no-undefined -Wl,--exclude-all-symbols`,
`-std=c11 -O2`, the output/source paths and `-lm`. `-fPIC` is unnecessary for PE.
The public header marks the query with `__declspec(dllexport)` when
`GOAT_NATIVE_BUILD` is defined; other symbols are not auto-exported. A handwritten
provider must use the same build define. Query and adapter pointers use explicit
`GOAT_NATIVE_CALL` (`__cdecl` on Windows, ordinary C elsewhere), including on i686.
`goat_native_query_v1_t` is the public query pointer type. The exported lookup name
is exactly `goat_native_query_v1`, verified through `GetProcAddress` in tests.

Generated DLLs link MinGW support libraries statically (`-shared -static -static-libgcc`),
including any required POSIX thread support. Users do not need to copy
`libgcc_s_*.dll` or `libwinpthread-1.dll` beside Goat. Windows system DLLs and the
selected Windows C runtime remain OS dependencies. The compiler installation must
provide the static runtime archives; this policy applies to newly compiled DLLs,
so existing artifacts must be recompiled. It does not change how `goat.exe` itself
is linked or broaden the loader's DLL search path.

`SearchPathA` resolves one executable, then `CreateProcessA` receives that explicit
application path and a CRT-quoted command line. Spaces, quotes and backslashes are
handled without `cmd.exe`, environment expansion or shell operators. Batch/shell
scripts are not executable wrappers on this path; use a native executable. Only the
NUL input and compiler-log output handles are inherited. Process, thread, file and
attribute-list resources are released on handled completion. Full unsigned Windows
process exit codes fit the result's `int64_t exit_code`; Win32 API and cleanup errors
have separate fields from errno.

Paths use the existing project's narrow Windows path convention (active Windows
ANSI code page), not a new UTF-8 filesystem contract. The current path-length limits
still apply. Broad Unicode filesystem support is a separate task.

A random sibling directory is created exclusively; it inherits its parent's ACL.
The temporary DLL already uses the final basename so that PE import metadata
continues to work after publication. Random directory names use Windows' built-in
BCrypt service, linked only on Windows; no external
library or C++ compiler is added. A nonempty regular, non-reparse output is published
with `MoveFileExA(REPLACE_EXISTING | WRITE_THROUGH)`. The destination is never deleted
first. In-use DLLs can block replacement: this is an error preserving the old DLL,
not a reason to unload somebody else's module or retry execution. Parallel builds
have separate temporary directories; concurrent publication to the same Windows
file may fail under sharing restrictions.

## Publication and cleanup

`compile_native_library` in `src/codegen/native_compiler.h` accepts generated source,
a compiler executable and a destination. On Linux it owns a fresh private `mkdtemp` directory
beside that destination, with a fixed source, output and log filename. Standard input
is `/dev/null`; compiler stdout and stderr are redirected to the log. `posix_spawnp`
passes arguments directly, and `waitpid` retries interrupted waits.

Only a successful child exit and a nonempty regular output file allow publication.
The library is renamed onto its destination on the same filesystem. This preserves
an existing destination on compile/link/publish failure. A destination symlink is
replaced as a directory entry; its target is not written. Concurrent builds use
independent directories and each publishes a complete library; the last rename wins.
This is atomic publication, not a power-loss durability guarantee.

Every handled path removes owned source, log and unpublished library files, then the
temporary directory. Cleanup errors are reported separately; a cleanup failure after
publication can report failure even though the new library is already present.
Abrupt termination of Goat itself (for example SIGKILL), a compiler creating extra
files, or filesystem failures can leave a directory behind. No signal-handler
cleanup or compiler timeout is provided in this step.

## Diagnostics and ownership

The result reports status, spawn/I/O error, exit code or terminating signal, cleanup
error, and owned diagnostic bytes. `destroy_native_compile_result` releases those
bytes. Exit code is -1 when no ordinary exit was observed. Captured diagnostic text
is capped at 65536 bytes, with an explicit truncation flag. The on-disk compiler log
is not size-limited during execution; file redirection avoids pipe-buffer deadlocks.
The CLI sends compiler output (including warnings on success) and failures to stderr.

Compilation checks that an artifact exists, not its ABI contents. The future loader
must validate the descriptor before execution. Native dispatch and recursion limits
remain disabled until their planned steps; this change does not modify `CALL`.

## Tests

`scripts/check_native_library.sh GOAT_BINARY OUTPUT_DIR` builds a real shared library
and links a separate ABI client against it. It covers numeric/recursive adapters,
source export, empty modules, default and explicitly selected compilers, filenames
with shell metacharacters, absent compilers, compiler diagnostics, signal termination,
invalid C, unresolved symbols, missing artifacts, large logs, publication failure,
input/symlink/hardlink protection, concurrent builds and temporary-file cleanup.
Unit tests cover request validation and the unsupported-platform stub without needing
a compiler. Linux CI runs the integration suite with both GCC and Clang.

`scripts/check_native_windows.sh` adds real DLL export lookup, loaded-DLL replacement
failure, native executable wrappers, large Windows exit codes and fast-math rejection
on all three Windows targets. The numeric suite calls real library adapters for
wrapping integer arithmetic, exact mixed comparisons beyond 2^53, NaN/infinities,
signed zero, subnormals and binary64 rounding (including i686 x87). Test-only Windows
loading does not implement the production loader planned for step 15.

Process construction follows Microsoft's [CreateProcess documentation](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessa)
and [C runtime quoting rules](https://learn.microsoft.com/en-us/cpp/c-language/parsing-c-command-line-arguments).


The Windows native backend targets Windows 8 APIs or newer. Its platform sources
include `lib/windows_target.h` before system headers, raising older MinGW defaults
for `_WIN32_WINNT` and `WINVER` to at least `0x0602` while preserving newer targets.
This makes `STARTUPINFOEXA`, restricted handle inheritance and safe DLL search
available independently of the toolchain's default target. It does not add support
for Windows XP or update an SDK that lacks these declarations entirely.
Windows CI compiles both platform sources with XP-era and Windows 10 target macros.
