# Modules: Agreed Contract

[Contents](index.md) · [Русский](../ru/modules.md)

Status: design agreed on October 20, 2026; discussion continues. These are requirements for future implementation, not a description of an existing loader. Issues #154 and #169 follow clarification of the contract. Where earlier access-control documents disagree, this page takes precedence.

## Build and distribution unit

One compiled module is one file without source code. It represents a library or program intended for independent distribution and repeated reuse. It contains:

- bytecode;
- metadata for imports and binding bytecode to native implementations;
- zero or more native libraries, potentially for multiple platforms.

Execution uses metadata to select a suitable library and implementations. Bytecode is used when no suitable native implementation exists. The loader may extract a library to a temporary directory or cache and load it through standard OS facilities. Prefer simple portable solutions using OS capabilities.

The binary format, filename extension, metadata schema, and exact platform compatibility rules remain undecided.

## Source files and module composition

A build specifies one entry source file. The module includes that file and all sources reachable through `include`, each once. Unused neighboring files are excluded. Imports of compiled modules become external dependencies.

Each source file has its own namespace. Declarations from different files do not merge; identical names are allowed. Each included file's bytecode is stored once, regardless of repeated inclusion. Exact placement restrictions for include/import still require clarification.

## Two distinct operations

```goat
const math = include "./math.goat";
const geometry = import "geometry";
```

`include` connects a source file within the current module and returns its scope object. It is not textual insertion. `import` connects a separate compiled module and returns its entry file's scope object. Another module's internal files cannot be imported directly.

Both operands must be string literals. Computed paths are forbidden so that module contents and external dependencies can be determined at build time.

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

The separator is `;` on Windows and `:` on Linux/macOS. Use the first matching file. A corrupt or incompatible file is an error; do not continue searching for another version. This allows a module and its dependencies to be distributed together in one directory.

Name-to-filename mapping, versions, path canonicalization, and the identity of dependencies found through different paths remain undecided.

## Initialization and cycles

Each source file is initialized once per program run. Repeated inclusion returns the same scope object with its current variable values.

Create the scope object before executing the file. Cycles are permitted: if the file is already initializing, return its existing object without restarting it. Reading a declaration that has not yet been initialized raises an error with the inclusion chain.

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

## Further discussion

The binary container and its versions, external dependency identity and versions, exact loading and initialization timing, details of cycles between compiled modules, previously returned references after failed initialization, platform and ABI descriptions, and build/packaging commands remain open. The examples above do not settle these questions.
