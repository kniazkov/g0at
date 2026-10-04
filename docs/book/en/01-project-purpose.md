# 1. Project Purpose

[Contents](index.md) · [Русский](../ru/01-project-purpose.md)

Edition 1. Implementation described: [commit 09d0cff, after PR #93](https://github.com/kniazkov/g0at/tree/09d0cffb08303b07c1c3af27ccbfc6af94466b97).

<a id="section-1-1"></a>

## 1.1. Goals

Goat is an experimental programming language and its implementation in C, developed by Ivan Kniazkov. The project is intended to investigate mechanisms for processing and executing programs, validate selected solutions, and demonstrate the results of this work.

The repository also serves as the author's technical portfolio. It provides material for consultations, presentations, and professional discussions: source code, examples, tests, and a change history. A statement about the system's design can be associated with a specific implementation and checked by running the relevant example or test.

This book describes the design of the existing system. The description is tied to the stated code revision. Planned capabilities are not included in the list of implemented mechanisms. The change history is used to explain decisions; current behavior is established from source code and the tests that check it.

<a id="section-1-2"></a>

## 1.2. Subject of Investigation

A language defines how a program is written and what its constructs mean. A language implementation reads the source text, checks its structure, builds an internal representation, and executes the program.

Goat uses bytecode for ordinary execution: a sequence of instructions for its own virtual machine. The virtual machine is a program written in C that executes these instructions and manages values and execution contexts. The sequence of the main stages is defined in the [launcher module](https://github.com/kniazkov/g0at/blob/09d0cffb08303b07c1c3af27ccbfc6af94466b97/src/cli/launcher.c).

Static analysis may run before execution: it computes information about a program without running it in the ordinary way. In particular, the analyzer determines possible values and types, call dependencies, and side effects. A side effect here means an observable action, such as output or a change to state external to a function. Unknown information remains unknown; it is not treated as proof that a transformation is permitted.

Native execution is available for some functions. The analyzer checks eligibility conditions, the generator produces C code, an external compiler builds machine code in a dynamic library, and Goat loads that library and calls a suitable function implementation. Such an implementation is called a specialization: it corresponds to a particular function and an ordered set of parameter types. Machine code is produced by the external C compiler; Goat generates its source text and arranges the use of the result.

This sequence makes it possible to investigate the relationship between information obtained by the analyzer and the transformations that the execution system permits on that basis.

<a id="section-1-3"></a>

## 1.3. Implemented Mechanisms

| Area | Implementation | Main sources |
|---|---|---|
| Reading and parsing | Lexical analysis, bracket grouping, reduction rules, and construction of the program tree | [scanner](https://github.com/kniazkov/g0at/tree/09d0cffb08303b07c1c3af27ccbfc6af94466b97/src/scanner), [parser](https://github.com/kniazkov/g0at/tree/09d0cffb08303b07c1c3af27ccbfc6af94466b97/src/parser) |
| Program representation | Tree nodes, scopes, name binding, and retention of original subtrees during replacement | [graph](https://github.com/kniazkov/g0at/tree/09d0cffb08303b07c1c3af27ccbfc6af94466b97/src/graph), [analysis.c](https://github.com/kniazkov/g0at/blob/09d0cffb08303b07c1c3af27ccbfc6af94466b97/src/analysis/analysis.c) |
| Execution | Bytecode generation, a stack-based virtual machine, objects, functions, closures, and exceptions | [codegen](https://github.com/kniazkov/g0at/tree/09d0cffb08303b07c1c3af27ccbfc6af94466b97/src/codegen), [vm](https://github.com/kniazkov/g0at/tree/09d0cffb08303b07c1c3af27ccbfc6af94466b97/src/vm), [model](https://github.com/kniazkov/g0at/tree/09d0cffb08303b07c1c3af27ccbfc6af94466b97/src/model) |
| Memory management | Compiler arenas, object reference counts, and mark-and-sweep garbage collection | [arena.c](https://github.com/kniazkov/g0at/blob/09d0cffb08303b07c1c3af27ccbfc6af94466b97/src/lib/arena.c), [object.h](https://github.com/kniazkov/g0at/blob/09d0cffb08303b07c1c3af27ccbfc6af94466b97/src/model/object.h), [gc.c](https://github.com/kniazkov/g0at/blob/09d0cffb08303b07c1c3af27ccbfc6af94466b97/src/vm/gc.c) |
| Static analysis | Abstract values and states, call and recursion analysis, effect information, and C generation eligibility checks | [analysis](https://github.com/kniazkov/g0at/tree/09d0cffb08303b07c1c3af27ccbfc6af94466b97/src/analysis) |
| Native backend | C generation for supported numeric specializations, library compilation and loading, implementation selection at calls, and conditional fallback to the VM | [native_pipeline.c](https://github.com/kniazkov/g0at/blob/09d0cffb08303b07c1c3af27ccbfc6af94466b97/src/codegen/native_pipeline.c), [function.c](https://github.com/kniazkov/g0at/blob/09d0cffb08303b07c1c3af27ccbfc6af94466b97/src/model/function.c) |
| Separate compilation | Saving and loading `.gbin`, association with a native library, and execution of a prepared artifact without source code or a compiler | [binary_program.c](https://github.com/kniazkov/g0at/blob/09d0cffb08303b07c1c3af27ccbfc6af94466b97/src/cli/binary_program.c), [binary.c](https://github.com/kniazkov/g0at/blob/09d0cffb08303b07c1c3af27ccbfc6af94466b97/src/vm/binary.c) |

The table lists areas of implementation. It does not imply that every language construct is supported at every stage. In particular, the ability to execute a function in the VM does not imply that C can be generated for it.

<a id="section-1-4"></a>

## 1.4. A Reproducible Example

The repository contains a [Fibonacci example](https://github.com/kniazkov/g0at/blob/09d0cffb08303b07c1c3af27ccbfc6af94466b97/example/fibonacci_analysis.goat). It defines a recursive function, that is, a function that calls itself, and computes the result for the argument `10`.

After building Goat, run the following commands from the repository root on Linux:

```sh
./goat --native off --save-analysis analysis.txt example/fibonacci_analysis.goat
./goat --native required --save-native native.txt example/fibonacci_analysis.goat
```

In Windows PowerShell, use:

```powershell
.\goat.exe --native off --save-analysis analysis.txt example/fibonacci_analysis.goat
.\goat.exe --native required --save-native native.txt example/fibonacci_analysis.goat
```

The second run requires an available compatible C compiler: `cc` on Linux or `gcc` on Windows by default. The `CC` environment variable selects a different compiler executable. The first run executes the program in the VM and saves an analysis log to `analysis.txt`. The second prepares a native library and saves a report of its use to `native.txt`. The report files are written to the current directory.

Both runs print `55` without a trailing newline. For the second run, the report contains `preparation=ready`, `succeeded=1`, and `retries=0`: the library was prepared, one call completed successfully in native code, and no VM retries occurred.

The matching result in this example confirms that the two execution paths work for this case. The successful-call counter distinguishes actual native execution from library preparation alone. The example does not establish equivalence for all possible programs and is not a performance measurement.

<a id="section-1-5"></a>

## 1.5. Evidence for Technical Conclusions

The repository provides several kinds of verifiable material:

- source code defines algorithms, structures, and failure conditions;
- [unit tests](https://github.com/kniazkov/g0at/tree/09d0cffb08303b07c1c3af27ccbfc6af94466b97/src/test) check individual mechanisms and their interactions;
- [analysis tests](https://github.com/kniazkov/g0at/tree/09d0cffb08303b07c1c3af27ccbfc6af94466b97/test/analysis) compare inferred information with expectations;
- [functional tests](https://github.com/kniazkov/g0at/tree/09d0cffb08303b07c1c3af27ccbfc6af94466b97/test/functional) and [validation scripts](https://github.com/kniazkov/g0at/tree/09d0cffb08303b07c1c3af27ccbfc6af94466b97/scripts) check execution results and component integration;
- the [CI configuration](https://github.com/kniazkov/g0at/blob/09d0cffb08303b07c1c3af27ccbfc6af94466b97/.github/workflows/build_and_test.yml) specifies checks for Linux with GCC and Clang and for Windows with MinGW variants;
- the commit and PR history records the sequence of changes and the discussion of decisions.

When citing a result, identify the code revision and the specific check. For measurements, also identify the platform, build configuration, and methodology. The presence of a check in the repository and its successful execution in a particular environment are distinct statements.

<a id="section-1-6"></a>

## 1.6. Limits of the Results

Static analysis is constrained by the supported constructs and computational limits. The native backend handles a subset of numeric functions. The absence of a proof leaves a function without the corresponding native specialization. Even proven function purity does not establish termination or the absence of exceptions; these properties require separate evidence.

Testing checks selected cases and conditions. It is not a complete proof of implementation correctness. An individual performance test characterizes its program and environment, rather than the speed of the language for an arbitrary workload.

The presence of a compiler, VM, analyzer, and native backend does not establish production readiness. These mechanisms do not imply guarantees of future-version compatibility, suitability for arbitrary workloads, or safe execution of untrusted code. In particular, a loaded dynamic library contains machine code and does not constitute an isolated execution environment.
