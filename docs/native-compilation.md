# Linux native compilation

`goat --save-library program.goat` analyzes and emits the same dependency-closed
numeric module as `--print-c`, compiles it, and writes `program.so` beside the
input. It does not execute the Goat program or load the library. Empty inventories
produce valid libraries with empty ABI descriptors. Unsupported specializations
remain omitted; compilation success does not mean the whole program is native.

The option requires `--optimize all`, permits `--save-c`, `--print-c`, analysis-file
and graph output, and rejects other `--print` modes. Without `--save-c`, the temporary
C file is removed. Explicit source/analysis exports are independent artifacts and
may remain if the subsequent library build fails. An input already named `.so`
is rejected to avoid overwriting it. Windows and other platforms currently return
an explicit unsupported-platform failure; DLL support belongs to step 14.

## Compiler invocation

`CC` selects one executable name or path; unset or empty uses `cc`. It must accept
GCC/Clang-style C and Linux linker options. Names without slashes are searched in
`PATH`. Paths with spaces are supported. No shell is involved: `CC="cc -O3"` is an
executable name, not a command line. Use a trusted executable wrapper for a compiler
launcher. The project and its ordinary interpreter do not require a C++ compiler.

The fixed argument vector is:

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

## Publication and cleanup

`compile_native_library` in `src/codegen/native_compiler.h` accepts generated source,
a compiler executable and a destination. It owns a fresh private `mkdtemp` directory
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
