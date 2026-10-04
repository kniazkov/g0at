# 1. Project Purpose

[Contents](index.md) · [Русский](../ru/01-project-purpose.md) · [Next chapter](02-implemented-language.md)

Edition 2. Implementation described: [commit 8d1fe86, including `println`](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0).

<a id="section-1-1"></a>

## 1.1. Why Build a Language

A short line of code rests on a whole sequence of decisions. Names and operations must be recognized, names must be associated with variables, evaluation order must be chosen, and values must be placed in memory. Even adding two numbers requires a contract: what happens on overflow, and what happens when one number is an integer and the other is real?

Goat provides a working system in which to examine these decisions. It is an experimental programming language and its implementation in C, developed by Ivan Kniazkov. It lets us follow the path from source text to a result, change an individual mechanism, and check the consequences. Static analysis (deriving information about a program without running it in the ordinary way) and native code generation (producing machine instructions for the processor) offer particularly useful examples: information about a program becomes evidence for choosing how to execute it.

The project also has a personal purpose: to demonstrate its author's engineering work. During a consultation or technical discussion, it is useful to be able to open a specific algorithm, show a test, and reproduce a result. The repository provides that basis: its sources, examples, and change history are available for inspection.

This book is organized around those same decisions. We will examine the problem each mechanism solves, the data it needs, and the limits of its capabilities. No prior knowledge of compiler design is required to begin: the necessary concepts are introduced as they appear.

<a id="section-1-2"></a>

## 1.2. From Text to Execution

For a person, a program begins as text. To execute it, Goat transforms that text into several internal representations. First it recognizes individual elements of the notation, then assembles them into a program tree. The tree records the structure of expressions and statements: for example, which two expressions are added and which condition a branch belongs to.

The ordinary execution path uses bytecode: a sequence of instructions for Goat's own virtual machine. The virtual machine (a program that executes bytecode instructions; VM for short) is written in C; it executes these instructions, stores intermediate values, and manages function calls. The main stages are connected in the [launcher module](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/cli/launcher.c).

Before execution, the analyzer tries to establish what is already known about the program. For example, it can obtain an exact value for the expression `2 + 3`. When constants are replaced with function parameters, less information may be available: the type is known, but the particular numbers are not. Computing this information without running the program in the ordinary way is called static analysis.

Besides values, the analyzer considers the program's actions. A function may print text or change a variable in an enclosing environment. Such actions are called side effects. They matter when transforming code: returning the same number does not by itself mean that two versions of a function behave identically. A pure function (one with no side effects or dependence on mutable external state) is convenient to analyze: its result can be considered in terms of its arguments. In Goat, proven purity is one of the conditions for eligibility for native compilation.

For some functions, Goat can prepare a native implementation. Native execution (running the prepared machine instructions directly on the processor) avoids the cost of processing each bytecode instruction in the VM and can speed up computation. The analyzer checks the necessary conditions, the generator writes suitable C code, an external compiler creates a dynamic library (a separate code file loaded while the program is running) containing machine code, and Goat loads it. An implementation for a particular function and an ordered set of parameter types is called a specialization.

This raises one of the book's central questions: what information is sufficient to select such an implementation while preserving program behavior? The answer combines analysis of types, effects, the function body, and its calls. An unknown property cannot simply be assumed suitable: switching to native code requires evidence.

<a id="section-1-3"></a>

## 1.3. A Map of the System

The names of the subsystems can make a language implementation difficult to navigate. The following table provides reference points, from reading source text to saving a prepared program. Later chapters examine each of these areas separately.

| Area | Implementation | Main sources |
|---|---|---|
| Reading and parsing | Lexical analysis (recognizing names, numbers, and operator symbols in text), bracket grouping, reduction rules, and construction of the program tree | [scanner](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/scanner), [parser](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/parser) |
| Program representation | Tree nodes, scopes, name binding, and retention of original subtrees during replacement | [graph](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/graph), [analysis.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/analysis/analysis.c) |
| Execution | Bytecode generation, a stack-based virtual machine, objects, functions, closures (functions with a retained environment), and exceptions | [codegen](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/codegen), [vm](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/vm), [model](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/model) |
| Memory management | Compiler arenas (memory regions for allocating and releasing data together), object reference counts, and mark-and-sweep garbage collection | [arena.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/lib/arena.c), [object.h](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/model/object.h), [gc.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/vm/gc.c) |
| Static analysis | Abstract values and states (descriptions of possible values and program states), call and recursion analysis, effect information, and C generation eligibility checks | [analysis](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/analysis) |
| Native backend (the subsystem that prepares machine code) | C generation for supported numeric specializations, library compilation and loading, implementation selection at calls, and conditional fallback to the VM | [native_pipeline.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/codegen/native_pipeline.c), [function.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/model/function.c) |
| Separate compilation | Saving and loading `.gbin`, association with a native library, and execution of a prepared artifact without source code or a compiler | [binary_program.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/cli/binary_program.c), [binary.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/vm/binary.c) |

These subsystems have different support boundaries. The VM can execute a function for which the C generator does not yet have the necessary mechanisms. This distinction will matter when reading analysis and native execution reports.

<a id="section-1-4"></a>

## 1.4. One Program, Two Paths

We can already observe the difference between VM and native execution. The repository includes a [Fibonacci example](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/example/fibonacci_analysis.goat). Its function is recursive: to compute a result, it calls itself with smaller arguments. The program computes the value for the argument `10`.

Let us run the same source in two ways. After building Goat, from the repository root on Linux:

```sh
./goat --native off --save-analysis analysis.txt example/fibonacci_analysis.goat
./goat --native required --save-native native.txt example/fibonacci_analysis.goat
```

For Windows PowerShell:

```powershell
.\goat.exe --native off --save-analysis analysis.txt example/fibonacci_analysis.goat
.\goat.exe --native required --save-native native.txt example/fibonacci_analysis.goat
```

The first run uses the VM and writes the analyzer's information to `analysis.txt`. The second prepares a native library and writes a report to `native.txt`. It requires a compatible C compiler: `cc` on Linux or `gcc` on Windows by default. The `CC` environment variable can select a different executable. Reports are saved in the current directory.

Both runs display `55` followed by a newline. That number alone does not show which path was taken. We therefore inspect `native.txt`:

```text
preparation=ready
succeeded=1
retries=0
```

This is an excerpt from the report: the library was prepared, one call completed successfully in native code, and no VM retries occurred. The counter records entry through the native adapter (code that connects a VM call to the machine-code implementation of a function); recursive calls within the generated function do not increase `succeeded`.

The example thus has two observable results: the computed number and confirmation of the execution path taken. It provides a starting point for the investigation that follows. Checking general properties and measuring performance are covered in separate chapters.

<a id="section-1-5"></a>

## 1.5. Checking the Explanation

When reading the book, it helps to move between the explanation and the code. The source shows how a mechanism works; a small test helps explain the behavior expected of it. An overflow boundary case, for example, often explains the choice of an algorithm better than an ordinary successful run.

The repository provides several kinds of material for this purpose:

- source code defines algorithms, structures, and failure conditions;
- [unit tests](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/test) check individual mechanisms and their interactions;
- [analysis tests](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0/test/analysis) compare inferred information with expectations;
- [functional tests](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0/test/functional) and [validation scripts](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0/scripts) check execution results and component integration;
- the [CI configuration](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/.github/workflows/build_and_test.yml) (automated building and checking of changes) specifies checks for Linux with GCC and Clang and for Windows with MinGW variants;
- the commit and PR history records the sequence of changes and the discussion of decisions.

A link to a specific code revision makes it possible to revisit the same explanation after later changes. A test result should also identify the test itself and the environment in which it passed. A speed measurement needs the platform, build configuration, and methodology: without them, the number loses much of its meaning.

<a id="section-1-6"></a>

## 1.6. Where the Boundaries Lie

An experimental project is particularly useful when its limitations are visible. Goat's analyzer handles supported constructs within computational limits. The native backend accepts a subset of numeric functions. If the necessary properties of a specialization could not be established, there is no basis for generating its native implementation.

Throughout the book, we will encounter properties that must be kept distinct. A function may leave external state unchanged while calling itself forever. Proven purity therefore does not prove termination. Similarly, a successful example run confirms that case, and a measured speedup applies to a particular program and environment.

Goat provides material for studying these distinctions, but its current mechanisms do not establish production readiness, future-version compatibility, or safe execution of untrusted code. In particular, a native library executes machine code without isolation. This book examines the existing implementation at the stated revision; possible future extensions are not treated as working capabilities.
