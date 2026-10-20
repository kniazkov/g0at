# Modules: Agreed Contract

[Contents](index.md) · [Русский](../ru/modules.md)

Status: design agreed on October 20, 2026; discussion continues. These are requirements for future implementation, not a description of an existing loader. Issues #154 and #169 follow clarification of the contract. Where earlier access-control documents disagree, this page takes precedence.

## Build and distribution unit

One compiled module is one file without source code. It represents a library or program intended for independent distribution and repeated reuse. It contains:

- bytecode;
- metadata for imports and binding bytecode to native implementations;
- zero or more native libraries, potentially for multiple platforms.

Execution uses metadata to select a suitable library and implementations. Bytecode is used when no suitable native implementation exists. The loader may extract a library to a temporary directory or cache and load it through standard OS facilities. Prefer simple portable solutions using OS capabilities.

Metadata uses JSON, matching the build description. The container is ZIP; bytecode and native libraries remain binary parts rather than JSON-encoded data. The filename extension, metadata schema, and exact platform compatibility rules remain undecided.

## Source files and module composition

A module build requires a description file, supplied to the compiler. It specifies the name, mandatory version, entry source file, and dependencies. The module includes the entry file and all sources reachable through `include`, each once. Unused neighboring files are excluded. Imports of compiled modules become external dependencies.

Each source file has its own namespace. Declarations from different files do not merge; identical names are allowed. Each included file's bytecode is stored once, regardless of repeated inclusion.

A standalone `.goat` file can still be run without a module description to execute code, test it, or print to the console. This does not build a distributable module. Every import in such sources must explicitly specify the dependency's full version.

## Build description

The description uses JSON:

```json
{
  "name": "geometry",
  "version": "1.2.0",
  "main": "src/main.goat",
  "dependencies": {
    "math": "2.0.0"
  }
}
```

Resolve `main` relative to the description file. Its filename and complete schema remain undecided.

A dependency version may be specified in the description or directly in an import:

```goat
const math = import "math";          // Version 2.0.0 from the description.
const oldMath = import "math@1.2.0"; // Explicitly selects another version.
```

An explicit version applies to that import without changing the default for unversioned imports. Both versions may coexist in one program with separate state. An unversioned import with no corresponding dependency in the description (or no description at all) is a compilation error. Never automatically select the latest or an arbitrary available version.

## Dependency verification and hashes

All imported compiled modules must be available at build time. The compiler reads their metadata for interface checking and analysis without running initialization code. A missing dependency is a compilation error. Dependencies must also be available at runtime.

Module metadata contains its name, version, and automatically computed main hash. It covers bytecode and common metadata; native parts are hashed separately as specified below. A consumer records each dependency's name, exact version, and expected main hash.

Each name/version pair has one build. Hashes are not directory names. Example layout (extension provisional):

```text
geometry/1.2.0/module.goatmod
geometry/2.0.0/module.goatmod
```

Old and new programs can use different library versions on the same computer. If a library's main content changes under the same version without rebuilding its consumer, a main hash mismatch at load time is an immediate error. Do not search for another build. Users need not select hashes in dependency descriptions; storing several hashes of one version in separate directories was rejected.

## Two distinct operations

```goat
const math = include "./math.goat";
const geometry = import "geometry";
```

`include` connects a source file within the current module and returns its scope object. It is not textual insertion. `import` connects a separate compiled module and returns its entry file's scope object. Another module's internal files cannot be imported directly.

Both operands must be string literals. Computed paths are forbidden so that module contents and external dependencies can be determined at build time.

Both operations are allowed in expressions, including function bodies and conditional branches. A dependency is established at build time even for an expression not yet executed; initialization occurs on first actual execution. The included file executes in its own scope, cannot see caller-local variables, and retains shared state across repeated accesses.

Two files in one module:

```goat
// math.goat
const epsilon = 0.000001;
public square = func(x) { return x * x; };
```

```goat
// main.goat
const math = include "./math.goat";
println(math.square(5)); // 25
// math.epsilon is inaccessible from this file.
external square = math.square;
```

A consumer obtains `square` through `import`. The author explicitly defines the entry file's external interface, including re-exported values from internal files.

## Access levels

| Declaration | Access boundary |
| --- | --- |
| No modifier | Its own source file only |
| `public` | All files within its module |
| `external` | Other modules as well |
| `protected` | Its object and descendants under the previously agreed rules |
| `private` | Its object under the previously agreed rules |

`external` is a standalone modifier that includes `public` accessibility. A declaration still permits only one modifier, preceding a variable or constant declaration; omitted `const` implies a constant:

```goat
external f = func() {};
external var counter = 0;
```

This changes the previous model: `public` formerly meant cross-module access, while unmodified declarations were module-visible. Implementation and older examples must be adapted to distinguish source-file and module identities. Existing object, capture, `clone`, and `flat` rules are not revoked; their interactions with the new access boundary need a separate review.

## Search

`include "./math.goat"` resolves relative to the source file containing the expression, without consulting the environment variable.

For `import "geometry"`, search in this order:

1. The directory containing the importing compiled module. When running sources directly, the directory of the source file containing `import`.
2. Directories in `GOAT_MODULE_PATH`, from left to right.

The separator is `;` on Windows and `:` on Linux/macOS. Use the first file matching the requested name and version. Corruption, incompatibility, or a mismatch with the consumer's recorded hash is an error; do not search for another build. This allows a module and its dependencies to be distributed together in one directory.

Exact name-to-path mapping, version syntax, path canonicalization, and the identity of dependencies found through different paths remain undecided.

## Initialization and cycles

Each source file is initialized once per program run. Repeated inclusion returns the same scope object with its current variable values.

Create the scope object before executing the file. Include cycles within one module are permitted: if the file is already initializing, return its existing object without restarting it. Reading a declaration that has not yet been initialized raises an error with the inclusion chain. Import cycles between modules are forbidden.

```goat
// a.goat
const b = include "./b.goat";
public x = 10;

// b.goat
const a = include "./a.goat";
println(a.x); // Error: x has not been initialized.
```

If b instead declares a function that reads `a.x` after initialization completes, the cycle itself does not prevent this.

If initialization throws, remember the failure and exception. Subsequent inclusion throws the saved exception without executing the file again; earlier side effects are not repeated.

## ZIP and native implementations

A module file is a ZIP container holding JSON metadata, bytecode, and native libraries. Goat embeds ZIP reading and writing through a small portable C library; external zip/unzip commands are not required. Select the library after checking size, license, and builds on supported platforms.

The main dependency hash covers bytecode and common metadata, including dependency names, versions, and expected main hashes. Exclude its own hash field, native libraries, and their descriptions. Each native library has a separate hash in its description. Adding a platform or rebuilding a native part does not require rebuilding consumers while the main content remains unchanged.

Hash part contents in a fixed order. ZIP entry ordering, timestamps, and compression do not affect identity. The algorithm and canonical metadata representation remain to be defined.

Native variant requirements cover OS and minimum version, architecture, bitness, required CPU extensions, system ABI/runtime (for example libc family and minimum version), and Goat native ABI version. An unknown or unverifiable mandatory requirement makes a variant unsuitable. Multiple simultaneously suitable variants are an ambiguity error. Exact platform schemas remain open; metadata alone cannot guarantee successful OS loading. Restricting dependencies to a predefined set of system libraries was not adopted.

Extract and load a native library on the first call that needs it. Use the VM if there is no compatible variant or native execution is disabled. If the OS cannot load the selected library, ordinary mode reports the reason and falls back to the VM. Remember failure for the remainder of the run: subsequent calls repeat neither the attempt nor the message. Under `--native required`, inability to load a required native implementation remains an error.

A hash mismatch means corruption and is an error, not a reason for VM fallback. Once a native function has begun execution, do not restart it on the VM and duplicate side effects. This rule concerns handling failed loading, rather than designing other native execution protocols.

## Merging and inspecting modules

A separate command combines builds of one module for different platforms into one file. Main hashes must match; otherwise fail. For identical platform requirements, identical native hashes produce one copy; different hashes are an error, with no automatic selection or replacement. The rule against ambiguous compatible variants still applies.

An inspection command displays name, version, main hash, dependencies, and native variants with requirements. It also attempts to load a suitable DLL/SO. It does not execute module bytecode or call its native functions, but the standard OS loader may run library initialization code (DllMain or SO constructors).

Perform trial loading in a child process so a crash does not terminate the inspection command. Terminate a hung child after a timeout and report the result. A command option controls the timeout; 5 seconds was proposed as the default. This is a diagnostic timeout, not a normal execution limit.

When creating the new task list, schedule a separate portable `lib` layer for starting the current executable with arguments, waiting/results, timeouts, and child termination. Also provide a distinct threading API: creation, joining, mutexes, and condition variables. A thread pool is not required yet. Threads do not replace processes for crash isolation.

## Dependency graph and preflight checks

Allow cyclic `include` only within one module. Reject cycles between compiled modules through `import`, reporting the dependency chain. Build dependencies first. The earlier proposals to exclude dependency hashes from the main hash and build mutually dependent groups together are withdrawn.

If A and B import the same version of C, load it once per run with a shared scope and state. Different expected main hashes for the same name/version pair are a dependency conflict.

Before running user code, check the entire dependency graph and everything verifiable without execution: availability, structure, versions, hashes, absence of cycles, and consistent requirements. This includes dependencies in branches not yet executed. Preflight does not run initialization code or cancel lazy native library loading.

Version, hash, and structure errors are loader errors that Goat `try/catch` cannot intercept. Exceptions from initialization code are ordinary catchable language exceptions.

## References to a file whose initialization failed

An include cycle may expose a scope object before initialization completes. After initialization fails, all subsequent property accesses on that scope throw the saved exception, even for already assigned properties.

Calling an escaped function defined in the failed file also throws the saved exception. This applies to copies made through `clone` and `flat`. Ordinary values already passed out are not revoked; earlier side effects are not rolled back.

While initialization is still in progress, calling an already declared function is allowed. It may use initialized values; reading a declaration not yet initialized raises an exception. The blanket call prohibition based on file state applies after initialization fails.

## Further discussion

Container format version, entry names and extension, complete JSON schema, hash algorithm and canonicalization, version syntax, exact platform/ABI requirements, path canonicalization, command names, and final packaging details remain open. The rules above replace earlier proposals for a single hash covering native code, fatal ordinary OS loading failures, and permitted import cycles.
