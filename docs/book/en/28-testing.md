# 28. Testing

[Contents](index.md) · [Русский](../ru/28-testing.md) · [Previous](27-observability.md) · [Next](29-performance-measurement.md)

Revision 3. Implementation described: [commit cf51b8c, including `for`](https://github.com/kniazkov/g0at/tree/cf51b8cb27a102d15260c7822ce503404462b0fd).

<a id="section-28-1"></a>

## 28.1. Different checks find different errors

A test establishes a particular correspondence: an operation returned an expected value, analysis retained a fact, a program printed expected text, or a failure happened before execution. One level cannot replace the others. A correct abstract result type does not prove machine-code correctness; matching stdout does not prove freedom from leaks.

The repository has three main runners: `unit_testing`, `analysis_testing`, and `functional_testing`. Integration scenarios in `scripts` connect the interpreter, external compiler, and loader. They exercise boundaries that are awkward to check with a single C function.

<a id="section-28-2"></a>

## 28.2. Unit tests and memory accounting

[unit_testing.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/unit_testing.c) registers tests from `src/test`. They cover containers, arenas, UTF-8, the lattice, states, parsing, objects, generation, ABI, and the binary format. Assertion failure reports its line and case name; a summary gives passed and failed case counts.

Ordinary Debug builds enable `MEMORY_DEBUG`: allocation wrappers maintain accounting. This helps check restoration of the initial allocation total after cleanup and locate lost blocks. Release disables this mode. Timings from these builds cannot be mixed without qualification.

Ownership tests are particularly relevant to arenas, shared abstract states, object cycles, exceptions, and library descriptors. The final reference must release a resource, while removing one reference must not destroy an object that is still in use.

<a id="section-28-3"></a>

## 28.3. Source-level analyzer tests

[analysis_testing.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/analysis_testing.c) reads names from `test/analysis/list.txt`. Each name has a `.goat` and `.expect` file. The program is parsed and analyzed; the VM is not run to check the result. Expectations select structured events rather than comparing the complete log text.

An expectation line has six fields: mode, event kind, row, a qualifying coordinate, selector, and expected value. The fourth field's meaning depends on the kind: declaration row for variable events, column for flags and specializations. A zero coordinate can mean no restriction.

| Mode | Requirement |
|---|---|
| `one` | Exactly one matching event with the required value |
| `last` | Matching events exist; the last has the required value |
| `none` | No matching events; the value field is `-` |

For functions, the selector specifies a signature such as `integer,real`; `-` means no parameters. Checks cover values as well as result status, effects, purity, C blockers, and expression representations. `TOP` from incomplete analysis therefore cannot pass as a successfully proven result.

Example from [false_without_else.goat](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/test/analysis/false_without_else.goat) and [false_without_else.expect](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/test/analysis/false_without_else.expect):

```goat
var x = 0;
if (false) {
 x = 9;
}
var y = x;
```

```text
none write 3 1 x -
one summary 5 5 y int=0
```

The first expectation forbids a write event in the unreachable branch. The second requires exactly one final `y = 0` summary; it does not require every other event to match a complete log.

<a id="section-28-4"></a>

## 28.4. Functional and integration checks

[functional_testing.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/functional_testing.c) uses a case directory containing `program.goat`, optional `input.txt`, and reference files `expected_output.txt` / `expected_error.txt`. A missing reference means its stream must be empty. Comparison ignores `\r`, not arbitrary whitespace. An error reference requires nonzero completion; the exact numeric exit code is not compared.

Ordinary mode runs each case with `--optimize none` and `all`. `compiled` checks separate bytecode compilation and execution; `auto` and `required` use only `all`. Failures retain `actual_output_*` and `actual_error_*`. Reproduction also depends on stdin, message language, and mode.

| Scenario | Significant boundary checked |
|---|---|
| `check_c_generation.sh`, `check_c_module.sh` | Numbers and evaluation order after C compilation |
| `check_native_abi.sh`, `check_native_loader.sh` | Adapter format, failures, metadata, and unloading |
| `check_native_call.sh`, `check_native_pipeline.sh` | Entry through `CALL`, results, resource retry |
| `check_native_modes.sh` | Mode behavior agreement and actual counters |
| `check_binary_program.sh` | Corrupt files, artifact pairs, compiler-free execution |
| `check_native_library.sh`, `check_native_windows.sh` | External compiler, paths, dependencies, build failures |

Loop checks include nested `for`, both directions of `if` nesting, bodies without braces, nearest-`if` binding of `else`, empty header slots, zero iterations, declaration scopes, captured per-iteration variables, returns, exceptions, integer wrapping, malformed syntax, and analysis-budget exhaustion. `scripts/check_for.py GOAT OUTPUT_DIR` additionally reparses regenerated source, runs saved bytecode, and compiles an infinite loop without executing it. It runs a saved native pair after removing the source and hiding the compiler, then checks successful native entries and exactly one resource retry. The script is included in Linux and Windows CI; the native-modes suite also compares the loop fixture in `off`, `auto`, and `required`.

<a id="section-28-5"></a>

## 28.5. Running tests and the CI matrix

From the repository root, with GCC and CMake installed:

```sh
bash scripts/build.sh
```

The script builds Debug and runs the three main runners. On Windows, with MinGW and CMake in `PATH`:

```powershell
.\scripts\build_mingw.cmd
```

This is not the complete set of separate CI integration jobs. For example, additionally check the ABI with:

```sh
bash scripts/check_native_abi.sh build/goat build/native-abi
```

[build_and_test.yml](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/.github/workflows/build_and_test.yml) defines Linux with GCC and Clang, plus Windows with GCC in MINGW32, MINGW64, and UCRT64 environments. It includes Debug and Release checks, Linux configuration with an unavailable C++ compiler, numeric checks with `-fsanitize=undefined,float-cast-overflow`, and a separate GCC x87 variant. A sanitizer detects particular error classes during execution; these checks do not mean the entire program always runs under a sanitizer.

<a id="section-28-6"></a>

## 28.6. What a green status leaves unproven

Style checking requires clang-format 18 and processes tracked `.c`/`.h` files. Lightweight Doxygen checking rejects warnings about malformed documentation but does not require documentation of every parameter. These check source documentation and formatting, not the contents of the bilingual book.

> [!CAUTION]
> Current CI does not automatically check semantic equivalence of the Russian and English book or run all `docs/book/examples` as a separate suite. Book changes require separate checks of links, tables, and examples. Passing CI also does not prove correctness for every program, absence of every leak, or support beyond the platform matrix.

For a regression (a previously fixed error returning), a small case with an observable difference is useful: a boundary number, a missing effect, an incorrect retry, or a corrupt field. A check merely repeating the implementation's algorithm can repeat its bug. Build composition is in [CMakeLists.txt](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/CMakeLists.txt); formatting rules are in [check_style.sh](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/scripts/check_style.sh) and [Doxyfile.check](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/scripts/Doxyfile.check).
