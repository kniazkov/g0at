# Appendix E. Launch Interfaces

[Contents](index.md) · [Русский](../ru/appendix-e-launch-interfaces.md) · [Previous](appendix-d-builtins.md) · [Next](appendix-f-implementation-map.md)

Revision 2. Implementation described: [commit 8d1fe86, including `println`](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0).

<a id="section-e-1"></a>

## E.1. General form

```text
goat [options] <input_file> [script arguments...]
```

Options are also recognized after the input filename. Option values are separate arguments; `--native=auto` is not supported. The first non-option word becomes the input path; subsequent words are retained as script arguments. Parsing is implemented in [options.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/cli/options.c).

> [!CAUTION]
> The `--` separator is not implemented. The retained script-argument list is not yet exposed to the program through a dedicated built-in interface. `--help` prints help, but current `main` returns nonzero because option parsing returns `NULL`; automation should account for this.

<a id="section-e-2"></a>

## E.2. Complete option list

| Option | Action |
| --- | --- |
| `--compile` | Save `.gbin`; do not execute the program body. |
| `--run` | Load and execute `.gbin` without source analysis or a compiler. |
| `--optimize none\|all` | Default `all`; `none` disables optimizing analysis, not required name binding. |
| `--native off\|auto\|required` | Default `off` for source, `auto` for `--run`. |
| `--print-native` | Preparation and counter report on stdout. |
| `--save-native file` | The same report in the specified UTF-8 file. |
| `--print-analysis` | Analysis-observation log on stdout. |
| `--save-analysis file` | The log in the specified UTF-8 file. |
| `--print-c` | C module on stdout without executing the Goat program. |
| `--save-c` | C module beside the source, with `.c` extension; no filename follows this flag. |
| `--save-library` | Library beside the source: `.so` on Linux, `.dll` on Windows; no body execution. |
| `--print-bytecode` | Text listing of generated or loaded bytecode. |
| `--print-source-code` | Goat text reconstructed from the current AST. |
| `--save-graph file.png\|file.svg` | AST image through Graphviz. |
| `-l`, `--lang`, `--language` + language | Runtime message language: `ru` selects Russian, other values select English. |
| `-w`, `--enable-warnings` | Enable compilation warnings. |
| `-h`, `--help`, `/?` | Print help. |

<a id="section-e-3"></a>

## E.3. Mode compatibility

`--compile` and `--run` are mutually exclusive and incompatible with `--print-c`, `--save-c`, and `--save-library`. `--run` also rejects explicit `--optimize`, analysis reports, graph output, reconstructed source, and warnings. Bytecode listings and native reports are allowed with `--run`.

Any C/library export requires `all` and cannot be combined with `--print-analysis`, `--print-source-code`, `--print-bytecode`, enabled native mode, or native reports. `--print-c`, `--save-c`, and `--save-library` may be combined with each other. Saving the analysis log to a file and graph output are not prohibited by this check.

`--native auto|required` is incompatible with `--optimize none`. `--compile --optimize none` with `native off` is allowed. `--compile` may print listings and save reports, but does not call the program body. Ordinary `--print-analysis`, `--print-source-code`, `--print-bytecode`, and `--save-graph` instead continue with normal execution after diagnostic output.

`off` neither prepares nor loads native code. `auto` permits continuing in the VM after preparation failure. `required` requires at least one bound function; it does not prohibit VM execution for an unmatched signature or a permitted resource retry. A faulty adapter after entry is not unconditionally replayed even in `auto`.

<a id="section-e-4"></a>

## E.4. Files and environment variables

`.c`, `.gbin`, `.so`, and `.dll` names replace the last extension of the input path. Users specify report and graph names. Native reports and output artifacts are checked for defined path conflicts; this is not general protection of arbitrary user files against overwriting. Choose a separate working directory before executing commands.

`CC` specifies one C-compiler executable, defaulting to `cc` on Linux and `gcc` on Windows. Do not append flags to `CC`. `GOAT_LANGUAGE` establishes initial message language; `--lang` is applied later, at launch, so option-parsing errors may use the initial language. `TMPDIR` affects the Linux temporary workspace; Windows uses the path returned by `GetTempPathA`.

Linux build scripts separately accept `CC` and `BUILD_DIR`; style checking uses `CLANG_FORMAT`. These belong to script interfaces, not additional Goat executable options.

<a id="section-e-5"></a>

## E.5. Streams and the native report

Program output and `--print-*` use stdout. Diagnostics use stderr. `--save-analysis` and `--save-native` keep reports separate from program output; they do not redirect the program's own stdout.

Native reports contain `key=value` lines. Keys are `mode`, `preparation`, `bound`, `omitted`, `attempts`, `succeeded`, and `retries`. Preparation states are `disabled`, `ready`, `empty`, `io-error`, `compile-error`, `load-error`, and `bind-error`; separate execution with a damaged library uses `load-error`. `bound` counts functions; `omitted` counts rejected generator candidates; the other three numbers count adapter-entry attempts, successful adapter completions, and VM retries. They do not count every internal machine-code call.

Ordinary success returns 0; launch, compilation, and execution errors return nonzero, normalized by `main` through `EXIT_FAILURE`. Chapter 27 describes the Graphviz exception. Sources: [main.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/main.c), [launcher.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/cli/launcher.c), [native_execution.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/cli/native_execution.c), [binary_program.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/cli/binary_program.c), [messages.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/resources/messages.c). Option checks: [test_native_options.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/test/test_native_options.c).
