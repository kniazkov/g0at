# Modules: Agreed Contract

[Contents](index.md) · [Русский](../ru/modules.md)

Status: design agreed on October 20, 2026; discussion continues. These are requirements for future implementation, not a description of an existing loader. Issues #154 and #169 follow clarification of the contract. Where earlier access-control documents disagree, this page takes precedence.

## Build and distribution unit

One compiled module is one file without source code. It represents a library or program intended for independent distribution and repeated reuse. It contains:

- bytecode;
- metadata for imports and binding bytecode to native implementations;
- zero or more native libraries, potentially for multiple platforms.

Execution uses metadata to select a suitable library and implementations. Bytecode is used when no suitable native implementation exists. The loader may extract a library to a temporary directory or cache and load it through standard OS facilities. Prefer simple portable solutions using OS capabilities.

Metadata uses JSON, matching the build description. The container is ZIP; bytecode and native libraries remain binary parts rather than JSON-encoded data. The extension is .gbin. The complete metadata schema and exact platform compatibility rules are refined below.

## Source files and module composition

A module build requires a description file, supplied to the compiler. It specifies the name, mandatory version, entry source file, and dependencies. The module includes the entry file and all sources reachable through `include`, each once. Unused neighboring files are excluded. Imports of compiled modules become external dependencies.

Each source file has its own namespace. Declarations from different files do not merge; identical names are allowed. Each included file's bytecode is stored once, regardless of repeated inclusion.

A standalone `.goat` file can still be run without a module description to execute code, test it, or print to the console. This does not build a distributable module. Every import in such sources must explicitly specify the dependency's full version.

## Build description

The description uses JSON:

```json
{
  "formatVersion": 1,
  "name": "geometry",
  "version": "1.2.0",
  "main": "src/main.goat",
  "dependencies": {
    "math": "2.0.0"
  }
}
```

Resolve `main` relative to the description file. Its extension is .gmod; the complete schema remains under design.

A dependency version may be specified in the description or directly in an import:

```goat
const math = import "math";          // Version 2.0.0 from the description.
const oldMath = import "math@1.2.0"; // Explicitly selects another version.
```

An explicit version applies to that import without changing the default for unversioned imports. Both versions may coexist in one program with separate state. An unversioned import with no corresponding dependency in the description (or no description at all) is a compilation error. Never automatically select the latest or an arbitrary available version.

## Dependency verification and hashes

All imported compiled modules must be available at build time. The compiler reads their metadata for interface checking and analysis without running initialization code. A missing dependency is a compilation error. Dependencies must also be available at runtime.

Module metadata contains its name, version, and automatically computed main hash. It covers bytecode and common metadata; native parts are hashed separately as specified below. A consumer records each dependency's name, exact version, and expected main hash.

Each name/version pair has one build. Hashes are not directory names. Example layout:

```text
geometry/1.2.0.gbin
geometry/2.0.0.gbin
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

Import uses one program-wide search list: local modules, explicit --module-dir, then GOAT_MODULE_PATH. Build/runtime details appear below. GOAT_MODULE_PATH uses `;` on Windows and `:` on Linux/macOS. Validate the first matching name/version file; corruption or an expected main-hash mismatch is an error without searching for another build.

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

Hash part contents in a fixed order. ZIP entry ordering, timestamps, and compression do not affect identity. Use SHA-256; agreed canonical JSON rules appear below.

Native variant requirements cover OS and minimum version, architecture, bitness, required CPU extensions, system ABI/runtime (for example libc family and minimum version), and Goat native ABI version. An unknown or unverifiable mandatory requirement makes a variant unsuitable. Multiple simultaneously suitable variants are an ambiguity error. Exact platform schemas remain open; metadata alone cannot guarantee successful OS loading. Restricting dependencies to a predefined set of system libraries was not adopted.

Extract and load a native library on the first call that needs it. Use the VM if there is no compatible variant or native execution is disabled. If the OS cannot load the selected library, ordinary mode reports the reason and falls back to the VM. Remember failure for the remainder of the run: subsequent calls repeat neither the attempt nor the message. Under `--native required`, inability to load a required native implementation remains an error.

A hash mismatch means corruption and is an error, not a reason for VM fallback. Once a native function has begun execution, do not restart it on the VM and duplicate side effects. This rule concerns handling failed loading, rather than designing other native execution protocols.

## Merging and inspecting modules

The separate goat-merge utility combines builds of one module for different platforms into one file. Main hashes must match; otherwise fail. For identical platform requirements, identical native hashes produce one copy; different hashes are an error, with no automatic selection or replacement. The rule against ambiguous compatible variants still applies.

The goat-inspect utility displays name, version, main hash, dependencies, and native variants with requirements. It also attempts to load a suitable DLL/SO. It does not execute module bytecode or call its native functions, but the standard OS loader may run library initialization code (DllMain or SO constructors).

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

## Names, versions, and extensions

Versions are exact string identifiers, such as `1.2.0` or `1.2.0-beta.1`. There are no ranges, version ordering, or automatic selection. Module names and versions contain only lowercase ASCII letters, digits, dots, underscores, and hyphens, starting with a letter or digit. Trailing dots and Windows reserved names, including extensions such as `con.txt`, are forbidden on every OS.

Extensions are `.goat` for sources, `.gmod` for JSON build descriptions, and `.gbin` for ZIP modules. Validate content and format versions, not just extensions.

Description, container, bytecode, and Goat ABI versions are independent of library versions. A .gmod requires `"formatVersion": 1`; missing or unsupported versions are errors. Unknown and duplicate JSON fields in descriptions and metadata are errors; introduce new fields through a new version of the corresponding format. Unsupported bytecode rejects the whole module even if suitable native code exists.

## Source canonicalization and diagnostics

Canonicalize include paths at build time, resolving symbolic links. Paths to the same file within a module share one scope, bytecode copy, and initialization.

Includes outside the description directory are allowed. The source belongs to the module being built; including it in another module creates separate code and state. User guidance: prefer small reusable modules imported through import over duplicating shared code through external includes.

Preserve logical source filenames, lines, columns, and function names. Do not include source text or absolute build-machine paths. Assign external sources distinct logical names; native diagnostics also need this mapping. Example:

```text
geometry@1.2.0: src/vector.goat:24:9, length
```

## Direct execution and built-in constants

Direct module execution runs its entry file without requiring a main function. Import runs that code as one-time initialization.

`__main__` is a built-in boolean constant, true only in the entry file of a directly executed module or standalone source. It is false in included files and imported modules. Assignment and redeclaration in any scope are forbidden.

```goat
external square = func(x) { return x * x; };
if (__main__) {
    println(square(5));
}
```

A function observes the value associated with its declaration file, not its caller. Clone/flat preserve this association. The value is determined at startup; neither bytecode nor native optimization may permanently substitute a launch-dependent value and discard a necessary branch.

`__dir__` is a built-in string constant containing the absolute directory of the main .gbin or main .goat. It is shared throughout the run, including libraries; it is not the library's directory. Assignment and redeclaration are forbidden, and the value is not baked in at build time.

Resources are not embedded yet. Application data resides beside the program and can be located using __dir__. Metadata-driven resource packaging and a reading API may be added later.

## Build directories and unified search

A repository root uses `<name>/<version>.gbin`, for example `geometry/1.2.0.gbin`. There is no separate version directory containing module.gbin. Metadata name/version must match the request; do not recursively discover arbitrary archives.

The compiler accepts --module-dir and creates this layout and missing directories. Without it, use modules beside the .gmod, independent of the working directory. A successful build replaces an existing version without confirmation through a temporary file; failure preserves the previous output.

Build search order: modules beside .gmod, then explicit --module-dir, then GOAT_MODULE_PATH. Check repeated directories only once.

Runtime search order: modules beside the main .gbin (or main .goat), then --module-dir, then GOAT_MODULE_PATH. One search list applies throughout the run, including indirect imports. Do not add each importing file's directory or infer a repository root from archive placement. These rules supersede the original importing-file-relative search.

Example portable distribution:

```text
my-app/
    app.gbin
    modules/
        geometry/
            1.2.0.gbin
    config.json
    data/
```

Explicit repository-root execution:

```sh
goat modules/app/1.0.0.gbin --module-dir ./modules
```

## Native build settings

Example description (the shown fields are agreed; the complete schema is still being designed):

```json
{
  "formatVersion": 1,
  "name": "geometry",
  "version": "1.2.0",
  "main": "src/main.goat",
  "dependencies": { "math": "2.0.0" },
  "native": { "mode": "auto", "target": "host" }
}
```

Modes off/auto/required are set in .gmod and can be overridden on the command line; default is auto. In auto, an absent C compiler produces a warning and a bytecode-only module. If a compiler is found but generated C fails compilation, fail the build rather than silently falling back to bytecode.

Required demands at least one native implementation in the module being built; dependency code does not count. Other functions may remain VM-only. No native implementations means a build error.

A build produces a variant for one target. Target is a string defaulting to host; --native-target overrides it. Resolve host into concrete platform requirements rather than storing the abstract word in .gbin. An explicitly selected other platform without a configured suitable compiler is an error, not a host build.

Use baseline target instructions by default, without automatic -march=native. Additional extensions require explicit configuration and metadata requirements. Supply C compiler paths and extra flags through command-line options or environment, not machine-specific paths in .gmod.

Mode, native target, C compiler selection, and flags are excluded from the main hash. With identical bytecode/common metadata, off/auto/required variants can be merged. Identical sources, main-compilation settings, and dependencies must produce identical main hashes on Windows and Linux, verified by tests. Exclude timestamps, absolute paths, and random identifiers from the main content.

Native libraries cannot directly call or inline one another; cross-module calls pass through Goat. Keep loaded modules and libraries alive until program termination.

## Validation and temporary files

Before execution, verify all native-part hashes, including foreign-platform libraries, without OS-loading them. Reject duplicate ZIP entry names, absolute paths, and .. components. Metadata references must unambiguously identify existing archive parts.

Use a separate temporary directory per run. Extract libraries on demand and reuse them within the run; no persistent cache yet. Clean up after unloading during normal shutdown. Cleanup failure produces a warning without changing the program's original exit code.

## Utilities and native ABI description

Inspection and merging use separate utilities:

```sh
goat-merge windows.gbin linux.gbin --output geometry.gbin
goat-inspect geometry.gbin
```

Include both in the distribution and package tests. Share reading, validation, and packaging code. Merge validates all inputs and matching names, versions, and main hashes before writing through a temporary file. Failure preserves existing output.

Inspect checks loading and all declared native symbols in a child process without calling functions. Corruption, loading failure, or a missing symbol produces a nonzero exit code. No variant for the current platform is a normal result reported to the user.

During normal execution, validate all symbols immediately after loading and before the first call. One missing symbol makes the whole selected library unusable: normal mode reports the cause once and uses the VM; required and inspect report an error.

The compiler generates an exported UTF-8 JSON string without a small fixed-size limit. It replaces the earlier short ABI structure proposal. Read it through an exported symbol without calling functions and compare it against .gbin metadata. It contains its format version, module name/version, main hash, Goat ABI version, platform requirements, native symbol list, and ABI signature identifiers. Partial example:

```json
{
  "formatVersion": 1,
  "module": "geometry",
  "version": "1.2.0",
  "mainHash": "...",
  "goatAbiVersion": 1,
  "target": { "os": "linux", "architecture": "aarch64" }
}
```

The complete function-list schema and service symbol name remain open. Validation checks the declared ABI; it does not prove machine-code behavior. A missing/incompatible description makes the library unusable. Store the DLL/SO's own hash externally to avoid self-reference.

## SHA-256, JSON, and implementation dependencies

Main and native-library hashes use SHA-256, represented as 64 lowercase hexadecimal characters. Hash JSON in canonical form: UTF-8, sorted object keys, fixed value encoding, no extraneous whitespace. Preserve array order. Formatting or field-order changes do not change the hash. Exclude only explicitly specified fields; exact canonical encoding rules still need specification.

Provide third_party/ for ZIP, JSON, and SHA-256 if needed, retaining each dependency's license, source, and exact version. Builds must not download them. Goat wrappers reside in lib. JSON support must detect duplicate keys and permit canonical output; specific libraries remain unselected.

## File visibility, copying, and object ownership

Clone and flat preserve each transferred field's original file, module, and access restrictions. Transferred methods retain their rights, with captures redirected to copied cells under the agreed rules. Removing prototypes through flat does not expose hidden fields.

```goat
// a.goat
public obj = {
    var secret = 42;
    public read = func() { return secret; };
};

// b.goat in the same module
const a = include "./a.goat";
var copy = a.obj.clone();
println(copy.read()); // 42.
// println(copy.secret); // Access error.
```

Code within one module may add properties to objects created by other files of that module. Object ownership governs extension; field provenance governs access:

```goat
// a.goat
public obj = { public var x = 1; };

// b.goat
const a = include "./a.goat";
a.obj.x = 2; // Accessible variable.
a.obj.y = 3; // Extension is allowed within the module.
println(a.obj.y); // 3.
```

Field y belongs to b.goat and, without a modifier, is accessible only to that file's code. Later obj.y = 4 from a.goat is an access error: an existing hidden field is not absent, and ordinary assignment does not create another same-key field. Another module cannot extend the original object even when obtained through external.

The clone/flat result belongs to the module whose code directly called the built-in operation. B can therefore extend its copy of an A object, while transferred fields retain A's protection. New fields belong to the file adding them. A wrapper declared in A and calling obj.clone() creates an A object even when B calls the wrapper.

Factories follow the same rule: an object created by an A function belongs to A regardless of its caller. B may modify accessible external variables on it but must make its own copy to extend it. A descendant created by B using an A prototype and the inheritance operator belongs to B; prototype fields retain A's protection, including the agreed protected rights.

## Observation from another file

Key iteration, print, str, and other string representations use the observing code's rights. Another file in the same module sees public and external fields; unmodified fields are visible only to their source file's code. Private/protected require the respective object rights; sharing a file is insufficient.

```goat
// a.goat
public obj = {
    var hidden = 10;
    public var shared = 20;
    external var exported = 30;
    private var secret = 40;
};

// b.goat
const a = include "./a.goat";
println(a.obj); // Only shared and exported.
var copy = a.obj.clone();
println(copy);  // Also only shared and exported.
```

The copy contains all four fields. Ordinary a.goat code outside the object's privileged context also sees hidden when printing, but not secret. Iteration in those contexts exposes the same accessible own keys. Full-state copying is not restricted to visible fields.

## Function rights: five agreed examples

Examples are independent. In the first four, a.goat and b.goat belong to one module. Rights follow the executing code's provenance, not the call site or where its reference is stored.

### 1. A function prints with its file's rights

```goat
// a.goat
public obj = {
    var hidden = 10;
    public var shared = 20;
};
public show = func(value) { println(value); };

// b.goat
const a = include "./a.goat";
println(a.obj); // Only shared.
a.show(a.obj);  // hidden and shared.
```

The author of a.goat may intentionally expose a function revealing that file's data. Calling it from b.goat does not remove show's rights.

### 2. A callback does not acquire its caller's rights

```goat
// a.goat
public obj = {
    var hidden = 10;
    public var shared = 20;
};
public inspect = func(callback) { callback(obj); };

// b.goat
const a = include "./a.goat";
const show = func(value) { println(value); };
a.inspect(show); // Only shared.
```

Show runs with b.goat rights. Reading value.hidden inside it would fail, despite being called from a.goat.

### 3. Returned closures retain rights and captures

```goat
// a.goat
public makeReader = func() {
    const obj = { var hidden = 10; };
    return func() { return obj.hidden; };
};

// b.goat
const a = include "./a.goat";
const read = a.makeReader();
println(read()); // 10.
```

This is a getter over a captured object. The function retains a.goat rights after returning without directly exposing the hidden object to its caller.

### 4. Storing a function as a method does not change its file

```goat
// b.goat
public readHidden = func(obj) { return obj.hidden; };

// a.goat
const b = include "./b.goat";
public obj = {
    var hidden = 10;
    public read = b.readHidden;
};
obj.read(obj); // Access error.
```

ReadHidden remains b.goat code. Storing its reference as a method of an a.goat object does not grant a.goat file rights.

### 5. Re-export preserves the original function's rights

```goat
// internal.goat
const obj = { var hidden = 10; };
public read = func() { return obj.hidden; };

// main.goat — library entry file
const internal = include "./internal.goat";
external read = internal.read;

// Another module's program
const lib = import "library@1.0.0";
println(lib.read()); // 10.
```

External exposes the function reference. The function retains internal.goat rights and captures; the hidden object is not directly exposed.

## Value access and function replacement: examples 6–10

Examples are independent. External exposes a specific declaration, not recursively the entire value.

### 6. Exporting an object does not automatically export its fields

```goat
// Library entry file
external obj = {
    public var internal = 10;
    external var shared = 20;
};

// Another program
const lib = import "library@1.0.0";
println(lib.obj.shared);   // 20.
// println(lib.obj.internal); // Access error.
```

The obj reference is externally accessible; internal remains accessible only to library module code. Check every field access.

### 7. Returning an object does not transfer function rights

```goat
// Library entry file
const obj = {
    var hidden = 10;
    external var shared = 20;
};
external getObject = func() { return obj; };

// Another program
const lib = import "library@1.0.0";
const obj = lib.getObject();
println(obj.shared); // 20.
// println(obj.hidden); // Access error.
```

GetObject can read hidden, but the recipient does not acquire that right. The library may deliberately expose the value through a getter.

### 8. A constant reference does not make an array immutable

```goat
// Library entry file
external items = [1, 2];

// Another program
const lib = import "library@1.0.0";
lib.items.push(3);  // Allowed: [1, 2, 3].
// lib.items = [4, 5]; // Error: items is a constant.
```

Field restrictions do not prohibit operations on an obtained mutable value. A library returns a copy through a function when independent data is needed; this does not imply automatic deep copying.

Include a separate task in the forthcoming GitHub task list for a function creating immutable wrapper objects, such as an array wrapper. Exact semantics, protection depth, and behavior when another reference modifies the underlying object remain undecided. Wrappers are not an already implemented part of modules.

### 9. Assignment preserves field provenance

```goat
// a.goat
public obj = { public var value = 1; };

// b.goat in the same module
const a = include "./a.goat";
a.obj.value = 2;
```

Only the value changes. Field value retains its a.goat provenance and public modifier; it does not become b.goat-local.

### 10. Field rights and replacement function rights are independent

```goat
// a.goat
public obj = {
    var hidden = 10;
    public var read = func() { return hidden; };
};

// b.goat in the same module
const a = include "./a.goat";
a.obj.read = func() { return a.obj.hidden; };
a.obj.read(); // Access error for hidden.
```

Read remains a public field of a.goat. The replacement function belongs to b.goat and does not inherit the replaced function's rights. Even a later call from a.goat does not give the replacement a.goat file rights.

## Re-export and assignment ordering: examples 11–15

Each example is independent; expected-error lines are checked separately.

### 11. Re-exporting a variable creates a separate cell

```goat
// internal.goat
public var counter = 0;

// main.goat
const internal = include "./internal.goat";
external var counter = internal.counter;
counter = 5;
println(internal.counter); // 0.
```

This is an ordinary declaration initialized with a value, not a live link between variables. For an array or object, both cells initially reference the same object; replacing one cell's reference does not change the other.

### 12. An internal file scope may be explicitly re-exported

```goat
// internal.goat
public helper = func() { return 10; };
external api = func() { return 20; };

// Library main.goat
external tools = include "./internal.goat";

// Another module's program
const lib = import "library@1.0.0";
println(lib.tools.api()); // 20.
// println(lib.tools.helper()); // Access error.
```

Direct import of an internal file remains forbidden. The author explicitly exposes a scope reference, but every field access is checked; public does not become external.

### 13. Indexing and dot access check the same rights

```goat
// a.goat
public obj = {
    var hidden = 10;
    public var shared = 20;
};

// b.goat
const a = include "./a.goat";
println(a.obj["shared"]); // 20.
// println(a.obj["hidden"]); // Access error.
```

Reads and writes through [] obey the same access rules as dot access. A definitely proven denial is a compilation error. If access cannot be determined statically, the VM checks it; computed keys do not bypass protection.

### 14. A compound assignment read error precedes the right-hand side

```goat
// a.goat
public obj = { var hidden = 10; };

// b.goat
const a = include "./a.goat";
const rhs = func() {
    println("Executed");
    return 1;
};
const update = func(target, key) {
    target[key] += rhs();
};
update(a.obj, "hidden"); // Access error; rhs is not called.
```

For runtime checking, the order is: evaluate receiver and key once, read the left-hand value, evaluate the right-hand side, apply the operation, and write the result. A denied read prevents right-hand evaluation: no message is printed and the field is unchanged. This makes side-effect ordering predictable. Effects already performed while evaluating the receiver and key are not rolled back.

### 15. Ordinary assignment checks writability before its right-hand side

```goat
// a.goat
public obj = { var hidden = 10; };

// b.goat
const a = include "./a.goat";
const rhs = func() {
    println("Executed");
    return 1;
};
const write = func(target, key) {
    target[key] = rhs();
};
write(a.obj, "hidden"); // Access error; rhs is not called.
```

Evaluate receiver and key once, then check access and whether writing is allowed. Evaluate the right-hand side only after a successful check. This also applies to assigning a constant or forbidden extension of another module's object. An error detected before the right-hand side prevents its side effects. This order is an implementation requirement, including optimizations; it does not claim that the new module model is already implemented.

## Assignment and side effects: examples 16–20

Examples are independent. These execution-order requirements apply to optimized and native code whenever the operation is supported. A definite static error may still reject a program before execution; the examples below describe runtime checking order.

### 16. Compound assignment checks writability before its right-hand side

```goat
const obj = { public limit = 10; };
const rhs = func() {
    println("Executed");
    return 1;
};
const update = func(target, key) {
    target[key] += rhs();
};
update(obj, "limit"); // Constant-assignment error, no output.
```

Permission to read is insufficient. Compound assignment also checks access and writability before evaluating its right-hand side. Rhs is not called if the result cannot be written. This adds an early writability check to example 14's order.

### 17. Use the value read before right-hand evaluation

```goat
var obj = { public var x = 10; };
const rhs = func() {
    obj.x = 100;
    return 2;
};
obj.x += rhs();
println(obj.x); // 12, not 102.
```

Compound assignment remembers the initial value 10. Rhs writes 100, then the operation computes 10 + 2 and stores 12. There is no second read of the left-hand value.

### 18. Exceptions prevent the final store but preserve side effects

```goat
var obj = { public var x = 10; };
const rhs = func() {
    obj.x = 100;
    throw "Error";
};
try {
    obj.x += rhs();
} catch (e) {
    println(obj.x); // 100.
}
```

The addition result is neither computed nor stored after the exception. Earlier changes made by the right-hand side remain. Assignment is not a transaction and does not roll back side effects.

### 19. Remember the destination object before the right-hand side

```goat
var obj = { public var x = 10; };
const original = obj;
const rhs = func() {
    obj = { public var x = 100; };
    return 2;
};
obj.x += rhs();
println(original.x); // 12.
println(obj.x);      // 100.
```

Replacing variable obj does not redirect the pending write. Its destination remains the original object. Ordinary obj.x = rhs() follows the same rule: do not evaluate the left-hand receiver again.

### 20. Remember the evaluated key as well

```goat
var obj = {
    public var x = 10;
    public var y = 20;
};
var key = "x";
const rhs = func() {
    key = "y";
    return 2;
};
obj[key] += rhs();
println(obj.x); // 12.
println(obj.y); // 20.
println(key);   // y.
```

Remember both the receiver and evaluated key x. Changing variable key does not redirect the write to y. Ordinary obj[key] = rhs() obeys the same rule. These examples establish single evaluation of receiver/key and, for compound assignment, a single read of the original value; side effects are not rolled back.

## Final write validation: examples 21–25

Examples are independent. A check before the right-hand side does not replace final validation: right-hand evaluation can change destination state. Revalidation does not re-evaluate receiver/key expressions or reread the initial compound-assignment value.

### 21. The right-hand side creates an inaccessible field

```goat
// a.goat
public obj = {};
public prepare = func() {
    obj.x = 100; // Field belongs to a.goat.
    return 2;
};

// b.goat in the same module
const a = include "./a.goat";
a.obj.x = a.prepare(); // Access error at the final write.
```

Initially x is absent and b.goat may extend an object within its module. Prepare creates x with a.goat file visibility. Final validation detects denied access. Field x remains 100 as a completed side effect; 2 is not stored.

### 22. An absent field is not created in advance

```goat
var obj = {};
const rhs = func() {
    println(obj); // { }.
    throw "Error";
};
try {
    obj.x = rhs();
} catch (e) {
    println(obj); // { }.
}
```

The preliminary check only permits a future write. The field appears after successful right-hand evaluation and final validation. No temporary placeholder cell is created; an exception leaves the empty object empty.

### 23. An accessible field created by the right-hand side is updated

```goat
// a.goat
public obj = {};
public prepare = func() {
    obj.x = 100; // Field belongs to a.goat.
    return 2;
};
obj.x = prepare();
println(obj.x); // 2.
```

Both writes execute a.goat code, so final validation succeeds. Update the cell already created by prepare, retaining its original provenance and metadata; do not replace it with a new cell.

### 24. An array index identifies a position, not the former element

```goat
var items = [10, 20, 30];
const rhs = func() {
    items.remove(0); // [20, 30].
    return 2;
};
items[1] += rhs();
println(items); // [20, 22].
```

Remember the array, index 1, and value 20. After removal, compute 20 + 2 and store 22 at current position 1 of the same array. The write does not follow the element shifted to another position.

### 25. A removed position fails final write validation

```goat
var items = [10, 20];
const rhs = func() {
    items.clear();
    return 2;
};
try {
    items[1] += rhs();
} catch (e) {
    println(items); // [ ].
}
```

The initial read succeeds. After clear, the saved index is out of bounds and final validation rejects the write. Do not automatically expand the array or roll back the completed clear.

## Exceptions and initialization states: examples 26–30

Examples are independent. Initialization failure means an exception escapes the file's top-level code, not merely that an exception occurred.

### 26. An internal catch permits successful initialization

```goat
// settings.goat
public var value = 10;
try {
    throw "No configuration";
} catch (e) {
    value = 20;
}

// main.goat
const settings = include "./settings.goat";
println(settings.value); // 20.
```

The exception is caught inside settings.goat and its top-level code completes normally. The file is successfully initialized; the caught exception does not block it.

### 27. Catching an included file's failure does not fail the caller

```goat
// broken.goat
println("Attempt");
throw "Error";

// main.goat
public var ready = false;
try {
    const broken = include "./broken.goat";
} catch (e) {
    ready = true;
}
```

Main initializes successfully; broken fails. Including broken again throws the saved exception without executing its code again; Attempt is printed once.

### 28. An uncaught exception interrupts the active initialization chain

```goat
// c.goat
println("C");
throw "Error C";

// b.goat
const c = include "./c.goat";

// main.goat
try {
    const b = include "./b.goat";
} catch (e) {
    println(e);
}
```

C and B remember failure because the exception escapes each file's initialization. Main catches it and continues. Including B or C again throws the saved exception without repeating execution; C is not printed again.

### 29. A function error after initialization does not fail the file

```goat
// library.goat
public fail = func() { throw "Call error"; };
public value = 42;

// main.goat
const lib = include "./library.goat";
try {
    lib.fail();
} catch (e) {
    println(e);
}
println(lib.value); // 42.
```

Library has already initialized successfully. The exception belongs to the fail call; it does not mark the file as failed or block other accesses. Another include returns the existing scope.

### 30. A consumer failure does not undo a ready dependency

```goat
// b.goat
println("Initializing B");
public value = 42;

// a.goat
const b = include "./b.goat";
throw "Error A";

// main.goat
try {
    const a = include "./a.goat";
} catch (e) {
    println(e);
}
const b = include "./b.goat";
println(b.value); // 42.
```

B completed initialization before A failed and remains usable. Initializing B is printed once. A's failure neither invalidates a successfully initialized dependency nor restarts it.

## Further refinement

The rules above are agreed; discussion continues one question at a time with examples. Complete schemas, canonical encoding details, service symbols, and supported system ABIs are refined during implementation design without changing agreed semantics.
