# Compiled programs

`--compile source.goat` parses, analyzes and generates bytecode, saves `source.gbin`
and stops before VM execution. `--optimize none|all` controls compilation as usual.
The default native mode is `off`, so a C compiler is unnecessary. Source diagnostics
and requested analysis/graph/bytecode outputs remain available.

`--compile --native auto|required source.goat` additionally builds a companion
`source.so` (Linux) or `source.dll` (Windows). The existing native pipeline validates
its ABI and metadata before saving bindings. `auto` reports preparation failures
and saves bytecode alone; `required` fails if no native function can be attached.
Neither mode calls Goat functions during compilation. Input and report paths cannot
alias an output artifact. `--save-library` remains an independent native-only export;
it cannot be combined with the new modes.

`--run source.gbin` loads and validates the saved program, then invokes the VM.
It never parses Goat, analyzes an AST, generates C or starts a compiler, including
when native preparation fails. Source-only flags and optimization flags are rejected.
`--print-bytecode`, `--print-native`, `--save-native`, and language selection work.
Ordinary source-file execution without either flag is unchanged.

## Companion selection

Run mode defaults to `--native auto`. Only native bindings recorded in the `.gbin`
enable companion loading. A bytecode-only artifact ignores any stale library beside
it. The library is located beside the input, using the same basename and the platform
extension; no search path or current-working-directory library lookup is used.
Renaming both files or moving them together preserves the bindings.

- `off`: bytecode only; the companion is never loaded.
- `auto`: validate the companion checksum, ABI and function metadata; diagnose a
  missing, incompatible or mismatched library and use bytecode instead.
- `required`: the same checks, but failure stops before the program executes.

Native dispatch still uses `CALL` and function-object descriptors. Exact signature
selection, unmatched-call fallback, recursion/resource retry, exception reporting
and library reference ownership are unchanged. The persistent library is never
removed by runtime cleanup. `required` requires bound native functions, not that
every call or every instruction runs natively.

## Format version 1

The 64-byte envelope starts with `GOATBIN1`, followed by seven little-endian u64
fields: format version (1), platform representation, bytecode size, binding count,
companion checksum, reserved zero and artifact checksum. The platform field stores
`sizeof(wchar_t)` in the low byte and a little-endian-host marker in bit 8.
The artifact checksum covers the entire file with its own field zeroed.

The existing packed bytecode buffer follows the envelope. Its header, instructions,
data descriptors and string/parameter data retain their native representation.
Each following 16-byte binding is two little-endian u64 values: the `FUNC`
instruction index and module-local function ID. Records are strictly ordered by
instruction index. Pointers, AST nodes and live library handles are never saved.

The reader bounds input to 256 MiB, checks envelope version and size, integrity,
section offsets, instruction codes, jump targets, string terminators, descriptor
ranges, parameter-name arrays and binding indices before execution. The VM also
checks instruction fetch bounds. This is structural validation, not a verifier of
arbitrary stack/control-flow programs or an execution sandbox. Treat bytecode and
native libraries as executable code from a trusted source. FNV-1a checksums detect
accidental corruption and mismatched companions; they are not authentication.

Version 1 requires matching byte order and `wchar_t` width, and binary64 doubles.
Native libraries additionally require the host OS/architecture and native ABI.
Do not assume `.gbin` portability between Linux and Windows. Changes to opcode
numbers, operands or serialized data semantics must increment the format version.

## Publication and failures

Each artifact is written through an exclusive temporary file in the destination
directory and atomically renamed into place. Compiler failure leaves an existing
pair unchanged. The companion is published before the `.gbin`; replacement of two
files is not a single filesystem transaction. If the second publication fails or a
reader races an update, a mixed pair is detected by the companion checksum and
follows the selected fallback policy. No mismatched native binding is silently used.
Bytecode-only rebuilding does not delete an older companion; its new `.gbin` does
not reference it.

## Tests

The functional runner's `compiled` mode runs the entire fixture list through
separate compilation and execution, with optimization disabled and enabled.
Compiler diagnostics are compared when compilation fails; otherwise output,
exceptions and exit status come from the loaded binary. The runner removes its
`.gbin` after each case.

`scripts/check_binary_program.sh` also tests relocated pairs without source or
compiler, actual native entry and bounded-recursion retry, caught/uncaught exceptions,
input at runtime, missing/wrong companions, failure preservation, stale libraries,
option/output collisions and write errors. CI runs this suite in Debug and Release
on Linux GCC/Clang and Windows MINGW32/MINGW64/UCRT64. Unit tests round-trip Unicode
data and reject truncation, corruption and structurally invalid records.
