# Appendix E. Launch Interfaces

[Contents](index.md) · [Русский](../ru/appendix-e-launch-interfaces.md) · [Previous](appendix-d-builtins.md) · [Next](appendix-f-implementation-map.md)

Updated for the implemented module system (#220); source baseline: [5b98271](https://github.com/kniazkov/g0at/tree/5b98271e26cb17bdc5589aaf827316016ba2e00a).

<a id="section-e-1"></a>

## E.1. General form

```text
goat [options] <input_file> [script arguments...]
goat [options] --interactive
```

Options are also recognized after the input filename. Option values are separate arguments; `--native=auto` is not supported. The first non-option word becomes the input path; subsequent words are retained as script arguments. Parsing is implemented in [options.c](https://github.com/kniazkov/g0at/blob/5b98271e26cb17bdc5589aaf827316016ba2e00a/src/cli/options.c).

> [!CAUTION]
> `--` terminates option parsing; subsequent values are literal. The retained script-argument list is not yet exposed to the program through a dedicated built-in interface. `--help` prints help, but current `main` returns nonzero because option parsing returns `NULL`; automation should account for this.

<a id="section-e-2"></a>

## E.2. Options

| Option | Action |
| --- | --- |
| `--compile` | Save `.gbin`; do not execute the program body. |
| `--run` | Load and execute `.gbin` without source analysis or a compiler. |
| `--optimize none\|all` | Default `all`; `none` disables optimizing analysis, not required name binding. |
| `--native off\|auto\|required` | Default `off` for direct `.goat`; `.gmod` uses its descriptor (default `auto`); ready ZIP modules default to `auto`. |
| `--cc EXECUTABLE` | Select one C compiler, without fallback to another executable. Inherits its environment. |
| `--native-probe-timeout SECONDS` | Positive integer probe budget, default 10. Does not limit user C compilation. |
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
| `-i`, `--interactive` | Execute commands line by line from the console; no input file is required. |
| `-h`, `--help`, `/?` | Print help. |

<a id="section-e-3"></a>

## E.3. Mode compatibility

`--compile` and `--run` are mutually exclusive and incompatible with `--print-c`, `--save-c`, and `--save-library`. `--run` also rejects explicit `--optimize`, analysis reports, graph output, reconstructed source, and warnings. Bytecode listings and native reports are allowed with `--run`.

Any C/library export requires `all` and cannot be combined with `--print-analysis`, `--print-source-code`, `--print-bytecode`, enabled native mode, or native reports. `--print-c`, `--save-c`, and `--save-library` may be combined with each other. Saving the analysis log to a file and graph output are not prohibited by this check.

`--native auto|required` is incompatible with `--optimize none`. `--compile --optimize none` with `native off` is allowed. `--compile` may print listings and save reports, but does not call the program body. Ordinary `--print-analysis`, `--print-source-code`, `--print-bytecode`, and `--save-graph` instead continue with normal execution after diagnostic output.

`off` neither prepares nor loads native code. `auto` permits continuing in the VM after preparation failure. `required` requires a compatible specialization in the main module (dependencies do not satisfy this requirement), with lazy loading; it does not prohibit VM execution for an unmatched signature or a permitted resource retry. A detected ABI violation is process-fatal: no catch, join continuation or VM replay, even in `auto`.

`--interactive` requires no input file and is incompatible with `--compile`, `--run`, and C/library export. Each line is compiled as a separate program with optimization disabled; the session context (top-level variables and functions) persists between lines, and compilation or execution errors are reported without resetting the context. The input prompt is `"> "`; it always begins on a fresh line, even after output that does not end with a newline. A line ending with a backslash does not run; input continues on the next line, prompted indented by two spaces (aligned under the typed text of the previous line), and all lines are joined without the backslashes when the input is submitted with Enter. Diagnostics number the session lines starting from the first entered one.

<a id="section-e-4"></a>

## E.4. Files and environment variables

For the standalone source pipeline, `.c`, `.gbin`, `.so`, and `.dll` names replace the last extension of the input path. Users specify report and graph names. Native reports and output artifacts are checked for defined path conflicts; this is not general protection of arbitrary user files against overwriting. Choose a separate working directory before executing commands.

`CC` specifies one C-compiler executable, otherwise compiler discovery probes the supported candidates. Do not append flags to `CC`. `GOAT_LANGUAGE` establishes initial message language; `--lang` is applied later, at launch, so option-parsing errors may use the initial language. `TMPDIR` affects the Linux temporary workspace; Windows paths use the common Unicode filesystem/process layer.

Linux build scripts separately accept `CC` and `BUILD_DIR`; style checking uses `CLANG_FORMAT`. These belong to script interfaces, not additional Goat executable options.

<a id="section-e-5"></a>

## E.5. Streams and the native report

Program output and `--print-*` use stdout. Diagnostics use stderr. `--save-analysis` and `--save-native` keep reports separate from program output; they do not redirect the program's own stdout.

Native reports contain `key=value` lines. Keys are `mode`, `preparation`, `bound`, `omitted`, `attempts`, `succeeded`, and `retries`. Preparation states are `disabled`, `ready`, `empty`, `io-error`, `compile-error`, `load-error`, and `bind-error`; separate execution with a damaged library uses `load-error`. `bound` counts functions; `omitted` counts rejected generator candidates; the other three numbers count adapter-entry attempts, successful adapter completions, and VM retries. They do not count every internal machine-code call.

Ordinary success returns 0; launch, compilation, and execution errors return nonzero, normalized by `main` through `EXIT_FAILURE`. Chapter 27 describes the Graphviz exception. Sources: [main.c](https://github.com/kniazkov/g0at/blob/5b98271e26cb17bdc5589aaf827316016ba2e00a/src/main.c), [launcher.c](https://github.com/kniazkov/g0at/blob/5b98271e26cb17bdc5589aaf827316016ba2e00a/src/cli/launcher.c), [native_execution.c](https://github.com/kniazkov/g0at/blob/5b98271e26cb17bdc5589aaf827316016ba2e00a/src/cli/native_execution.c), [binary_program.c](https://github.com/kniazkov/g0at/blob/5b98271e26cb17bdc5589aaf827316016ba2e00a/src/cli/binary_program.c), [messages.c](https://github.com/kniazkov/g0at/blob/5b98271e26cb17bdc5589aaf827316016ba2e00a/src/resources/messages.c). Option checks: [test_native_options.c](https://github.com/kniazkov/g0at/blob/5b98271e26cb17bdc5589aaf827316016ba2e00a/src/unit_test/test_native_options.c).

## E.6. Module CLI and companion tools

```sh
goat build project/app.gmod --module-dir ./modules --native off
goat run project/app.gmod --native off
goat run modules/app/1.gbin --module-dir ./modules
goat --list-native-targets --cc gcc
goat-inspect --json --module-dir ./modules modules/app/1.gbin
goat-merge combined.gbin first.gbin second.gbin
```

These command forms assume existing valid input files. `build` requires `.gmod`;
`--compile .gmod` also publishes an archive atomically without executing initializers.
Direct/run `.gmod` executes an in-memory snapshot without publishing. `run` accepts
source/descriptor/archive; `--run` accepts binary input only. `--module-dir DIR`
selects an additional search/output root relative to the initial working directory;
`--native-target TARGET` overrides the descriptor target independently of mode.
Default output is `modules/<name>/<version>.gbin` beside the real `.gmod`.
`GOAT_MODULE_PATH` adds search roots after the real local `modules` and explicit root.

Module inputs reject C/source/analysis/graph export with a diagnostic. Their mandatory
interface analysis still runs with `--optimize none`; neither setting promises aggressive
module AST rewriting. See [CLI](../../module-cli-api.md) for all compatibility checks.
[Inspect](../../module-inspect-api.md) validates the complete dependency graph; its optional
native probe is an isolated subprocess, not a security sandbox. [Merge](../../module-merge-api.md)
validates archives without dependency lookup, native loading or code execution. Conflicting
information requires explicit resolution; author ordering may require interactive input.
`exit(code)` may select a successful process status other than zero; see [exit](../../module-exit-api.md).
