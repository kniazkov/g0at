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

## Saved exceptions and finally: examples 31–35

Examples are independent; files within each example belong to one module.

### 31. Rethrow the same value without cloning

```goat
// broken.goat
throw { public var message = "Error"; };

// main.goat
try {
    const first = include "./broken.goat";
} catch (e) {
    e.message = "Clarified error";
}
try {
    const second = include "./broken.goat";
} catch (e) {
    println(e.message); // Clarified error.
}
```

The loader retains the same exception object, without an implicit copy or freezing. A handler may alter accessible mutable fields or nested values; subsequent handlers see these changes. This is an intentional consequence of ordinary value semantics. A shallow copy would not isolate nested objects. A separate immutable loader diagnostic representation is not approved by this decision.

### 32. Propagating one exception preserves a shared reference

```goat
// c.goat
throw { public var message = "Error C"; };

// b.goat
const c = include "./c.goat";

// main.goat
try {
    const b = include "./b.goat";
} catch (e) {
    e.message = "Clarified error";
}
try {
    const c = include "./c.goat";
} catch (e) {
    println(e.message); // Clarified error.
}
```

The exception passes through B without replacement or copying. B and C retain the same reference, so a change made while handling B's inclusion is visible when C is included again.

### 33. Throwing a replacement exception separates saved values

```goat
// c.goat
throw "Error C";

// b.goat
try {
    const c = include "./c.goat";
} catch (e) {
    throw "Error B";
}

// main.goat
try {
    const b = include "./b.goat";
} catch (e) {
    println(e); // Error B.
}
try {
    const c = include "./c.goat";
} catch (e) {
    println(e); // Error C.
}
```

Each file remembers the exception that ultimately escapes its own initialization code. B's replacement error does not replace C's saved error.

### 34. An exception from finally becomes the final failure

```goat
// broken.goat
try {
    throw "First error";
} finally {
    throw "Cleanup error";
}

// main.goat
try {
    const broken = include "./broken.goat";
} catch (e) {
    println(e); // Cleanup error.
}
```

Subsequent inclusion also throws Cleanup error. The loader remembers the exception that ultimately escaped, does not separately retain the first error, and does not alter ordinary try/finally semantics.

### 35. Finally is not repeated on subsequent inclusion

```goat
// broken.goat
try {
    throw "Error";
} finally {
    println("Cleanup");
}

// main.goat
try {
    const first = include "./broken.goat";
} catch (e) {
    println(e);
}
try {
    const second = include "./broken.goat";
} catch (e) {
    println(e);
}
```

Output:

```text
Cleanup
Error
Error
```

Finally runs during the first initialization attempt. Subsequent include only throws the saved exception; neither the main code nor cleanup runs again.

## Partially initialized scopes: examples 36–40

Examples are independent; include cycles occur within one module. They do not permit import cycles between modules.

### 36. Premature reads are catchable

```goat
// a.goat
const b = include "./b.goat";
public x = 10;

// b.goat
const a = include "./a.goat";
try {
    println(a.x);
} catch (e) {
    println("Too early");
}

// main.goat
const a = include "./a.goat";
println(a.x); // 10.
```

Reading a known but uninitialized cell raises an ordinary catchable exception. B catches it and completes; A resumes and declares x. Both files initialize successfully. The premature read itself does not mark A as failed.

### 37. Writing an uninitialized declaration is forbidden

```goat
// a.goat
const b = include "./b.goat";
public var x = 10;

// b.goat
const a = include "./a.goat";
try {
    a.x = 20;
} catch (e) {
    println("Too early");
}

// main.goat
const a = include "./a.goat";
println(a.x); // 10.
```

Declaration x is already known but has not executed. Its cell is not an absent field that ordinary assignment may create. Even an accessible variable cannot be written before its declaration. The exception is caught; after B returns, A's initializer stores 10.

### 38. Observation skips declarations that are not ready

```goat
// a.goat
public first = 1;
const b = include "./b.goat";
public second = 2;

// b.goat
const a = include "./a.goat";
println(a); // Only first.

// main.goat
const a = include "./a.goat";
println(a); // first and second.
```

Printing and iterating a partially initialized scope skip declarations not yet executed: do not substitute null values or enumerate their keys. Ordinary visibility checks also apply. Directly reading a.second from B still raises an exception; omission during observation does not mean the cell is absent.

### 39. Copying an unfinished scope is forbidden

```goat
// a.goat
public first = 1;
const b = include "./b.goat";
public second = 2;

// b.goat
const a = include "./a.goat";
try {
    const copy = a.clone();
} catch (e) {
    println("Cannot copy yet");
}
```

Clone and flat on a file scope before initialization completes raise a catchable exception. Neither discard unfinished cells nor transfer them into a copy where their declarations will never run. Copying is permitted after successful initialization. This restriction concerns the file scope itself, not every ordinary object created during initialization.

### 40. Inheritance from an unfinished scope is allowed

```goat
// a.goat
const b = include "./b.goat";
public value = 10;
public child = b.child;

// b.goat
const a = include "./a.goat";
public child = a -> {};

// main.goat
const a = include "./a.goat";
println(a.child.value); // 10.
```

Inheritance retains a prototype reference without copying unfinished cells. Reading value through the descendant before its declaration executes raises an exception, just like a direct read from A. After successful initialization, the value is accessible through the prototype under ordinary access checks.

## Scope prototypes and copying: examples 41–45

### 41. A failed prototype does not block a descendant's own fields

```goat
// a.goat
public value = 10;
const b = include "./b.goat";
throw "Error A";

// b.goat
const a = include "./a.goat";
public child = a -> { public own = 20; };

// main.goat
try {
    const a = include "./a.goat";
} catch (e) {
    println(e);
}
const b = include "./b.goat";
println(b.child.own);   // 20.
// println(b.child.value); // Saved exception: Error A.
```

B completed initialization successfully, so child's accessible own fields remain accessible. Lookup reaching the failed A scope throws its saved exception: inheritance does not bypass the failed-scope guard.

### 42. Flat returns no partial result with a failed prototype

In the state from example 41:

```goat
try {
    const flat = b.child.flat();
} catch (e) {
    println(e); // Error A.
}
println(b.child.own); // 20.
```

Built-in flat must transfer prototype state and therefore throws A's saved exception. It neither skips the failed prototype nor returns a partial object containing only own. The original object is unchanged.

### 43. Cloning a descendant retains the failed prototype reference

In the same state:

```goat
const copy = b.child.clone();
println(copy.own); // 20.
// println(copy.value); // Saved exception: Error A.
```

Built-in clone copies own fields and retains prototype references without reading their fields. Cloning the descendant is therefore allowed, while access through the failed prototype remains an error. This does not permit cloning the failed file scope itself.

### 44. Flat also rejects a prototype still being initialized

```goat
// a.goat
public first = 1;
const b = include "./b.goat";
public second = 2;

// b.goat
const a = include "./a.goat";
public child = a -> { public own = 3; };
try {
    const copy = child.flat();
} catch (e) {
    println("Not yet");
}
```

Child already exists, but A has not completed initialization. Flat raises a catchable exception for the unfinished prototype. The same call is allowed after A completes successfully.

### 45. Validate the entire prototype graph

```goat
// a is a previously obtained file scope reference.
const middle = a -> { public middleValue = 2; };
const leaf = middle -> { public own = 3; };
const copy = leaf.flat();
```

If A is still initializing, flat throws an unfinished-initialization exception. If A failed, it throws the saved exception. Check the entire chain and all multiple-inheritance branches, not only immediate prototypes. Do not return a partial result. This supplements the agreed prototype traversal preserving lookup priority and transferring each object only once.

## Module identity and strict search: examples 46–50

### 46. Different versions are separate entities and access boundaries

```goat
// Code in geometry@2.0.0
const old = import "geometry@1.0.0";
println(old.api); // Allowed if api is external.
// println(old.internal); // Error if internal is public.
// old.extra = 1; // Error: extending another module's object.
```

Geometry@1.0.0 and geometry@2.0.0 are separate modules with distinct state and rights. A shared name does not grant internal access to another version. Interaction uses external, just as between any other distinct modules.

### 47. Physical copies of one build do not create distinct instances

```goat
// Entry file of geometry@1.0.0
external var counter = 0;

// consumer-a.goat
const geometry = import "geometry@1.0.0";
geometry.counter = 7;

// consumer-b.goat, executed after consumer-a.goat
const geometry = import "geometry@1.0.0";
println(geometry.counter); // 7.
```

Even if the same version with the same main hash exists in both local modules and GOAT_MODULE_PATH, there is one entity per run. Select a file using the agreed search order, initialize once, and share its state. A different physical-copy path does not create a new module.

### 48. Do not automatically combine native parts from different copies

```text
app/modules/geometry/1.0.0.gbin   — bytecode only
shared/geometry/1.0.0.gbin        — same main hash + Linux x64
```

On Linux, select the entire local file and use the VM in ordinary mode. A more suitable native part in a later directory does not change selection. Required follows the agreed strict-mode rules. The author combines variants using goat-merge; the runtime loader does not assemble a module from multiple copies. The first found module wins even if a later copy could be better.

### 49. A found but unreadable file stops search

```text
app/modules/geometry/1.0.0.gbin — exists, read permission denied
shared/geometry/1.0.0.gbin      — readable
```

Report a loading error with path and cause; do not fall through to shared. Search continues for an absent path, not a faulty candidate.

This is particularly important during development: a broken nearby build must not be hidden by loading a working but older copy. The same rationale applies to the already agreed format, incompatibility, and main-hash mismatch errors.

### 50. Failure to inspect a directory is not proof of absence

```text
app/modules/geometry/ — access permissions prevent checking its contents
shared/geometry/1.0.0.gbin — valid module
```

If the loader cannot reliably establish that the local file is absent, stop with an access error. Continue only when the requested path is reliably absent. Directory-check failures do not become missing dependencies or switch execution to another copy.

## Search paths and diagnostics: examples 51–55

### 51. An explicit relative module-dir is working-directory-relative

```sh
# Working directory: /work
goat build ./project/app.gmod --module-dir ./shared
```

Output is /work/shared/<name>/<version>.gbin. Without --module-dir, use /work/project/modules/<name>/<version>.gbin: the default is relative to the project description. An explicit argument is an ordinary command-line path. An absolute --module-dir is allowed and is not prefixed with the working directory.

### 52. Relative environment paths are fixed at startup

```text
Working directory: /work
GOAT_MODULE_PATH=./shared:./vendor
```

These resolve to /work/shared and /work/vendor. Resolve relative entries once against the startup working directory and retain absolute paths. Later working-directory changes do not alter search. Each GOAT_MODULE_PATH entry may also be absolute; absolute entries are not working-directory-relative. Apply the same one-time resolution to an explicit --module-dir.

### 53. Ignore empty list entries

```text
Linux:   /opt/goat/modules::/home/user/modules:
Windows: C:\goat\modules;;D:\modules;
```

Each example specifies two directories. An empty entry does not mean the current directory. Use an explicit dot (.) to include it, resolved according to example 52.

### 54. An unset environment variable is not an error

```sh
goat app.gbin
```

If GOAT_MODULE_PATH is unset or contains only empty entries, search uses modules beside the main file and explicit --module-dir, if supplied. A program without external dependencies does not require search directories to exist. Report a missing dependency only when a required module cannot be found; errors checking existing paths follow strict rules 49–50.

### 55. Distinguish missing dependencies from faulty candidates

Example diagnostic when a dependency is absent everywhere:

```text
Module geometry@1.2.0 not found.
Required by app@1.0.0.

Checked:
  /work/app/modules/geometry/1.2.0.gbin
  /work/shared/geometry/1.2.0.gbin
  /opt/goat/modules/geometry/1.2.0.gbin
```

Show the name, exact version, consumer, and all checked paths in search order. If search stops on denied access, corruption, or another candidate error, report that specific cause and stopping path rather than incorrectly reporting a missing module. Do not describe later directories as checked.

## Dependency inclusion and independence: examples 56–60

### 56. An unused manifest entry produces a warning

.gmod fragment:

```json
{
  "dependencies": {
    "geometry": "1.2.0",
    "unused": "2.0.0"
  }
}
```

If sources use only import "geometry", unused produces a warning, not an error. It is required neither at build time nor at runtime and is not recorded in .gbin dependencies. A description entry supplies a version for a possible import; it does not import a module by itself. This is a description fragment, not a complete .gmod.

### 57. An import in unreachable code counts as use

```goat
if (false) {
    const geometry = import "geometry@1.2.0";
}
```

Geometry remains a compiled-module dependency and is checked at build time and before execution. It is not initialized in this example. Dependencies are determined from sources; optimization and unreachable-code removal do not change the set.

### 58. An explicit different version does not use the manifest version

.gmod fragment:

```json
{
  "dependencies": {
    "geometry": "1.2.0"
  }
}
```

The only source import:

```goat
const geometry = import "geometry@2.0.0";
```

The output depends only on geometry@2.0.0. The unused 1.2.0 setting produces a warning; that version's file is not required. An explicit version does not change the default for unversioned imports.

### 59. Do not embed external dependencies in consumers

```text
app@1.0.0 → geometry@1.2.0 → math@2.0.0
```

App contains its own code and a geometry dependency record; geometry contains its own code and a math dependency record. Execution requires three separate .gbin files; the loader validates the entire chain. Dependency contents are not copied into consumers.

This is a program-organization principle: large programs should be modular. The format does not provide fat modules embedding external libraries. Multiple source files and multiple native variants of a module's own code remain allowed.

### 60. Applications do not override library dependencies

```text
app@1.0.0 → geometry@1.2.0 → math@2.0.0
app@1.0.0 → math@3.0.0
```

App uses math@3.0.0; geometry continues using math@2.0.0 recorded by its own build. Load both versions with separate state and rights. The application description does not globally override versions.

Each module fixes names, exact versions, and expected main hashes of its own dependencies. This independence provides deterministic dependency resolution at startup: library code receives the dependency it was built against. It does not promise deterministic user-code behavior with arbitrary external inputs.

## Runtime snapshots and standalone utilities: examples 61–65

### 61. Keep module contents in RAM

During startup preparation, load all contents of selected .gbin files into RAM, validate them, and retain them until process termination. Do not reread the original archives afterwards.

```text
1. The program validates geometry@1.2.0.
2. A developer rebuilds and replaces its .gbin on disk.
3. The program first calls a native geometry function.
```

At step 3, extract the library from validated in-memory data. On-disk replacement does not mix old bytecode with new native code and becomes visible only on the next run. Lazy OS loading of DLL/SO remains; it does not mean lazy reading of the original archive.

### 62. Memory exhaustion terminates the process

```text
App has been loaded.
There is insufficient memory for geometry@1.2.0.
Terminate before executing user code.
```

Apply Goat's general memory-exhaustion rule. Do not partially load the module, select another copy, or attempt to continue execution.

### 63. Inspect does not require the module's external dependencies

```text
Geometry@1.2.0 depends on math@2.0.0.
Only geometry.gbin is present on the computer.
```

```sh
goat-inspect geometry.gbin
```

Inspect validates the archive itself, its hashes, and the suitable native library under the agreed rules. Show the math dependency as metadata; its missing file is not an inspection error. Successful inspection of one file does not establish availability of the full graph. Full graph validation remains mandatory when executing a program.

### 64. Merge does not require external dependency files

```text
windows.gbin → math@2.0.0, dependency main hash H
linux.gbin   → math@2.0.0, dependency main hash H
```

```sh
goat-merge windows.gbin linux.gbin --output geometry.gbin
```

If the input modules' own main hashes match and their archives are valid, missing math does not prevent merging. Preserve dependency name, version, and expected main hash. Check dependency availability when building a consumer or executing, not when packaging native variants.

### 65. Merge never OS-loads native libraries

```sh
# Running on Linux.
goat-merge windows.gbin linux.gbin --output geometry.gbin
```

Merge validates archive structure, metadata, hashes, and merge rules. It does not load DLL/SO files, even for the current platform, or run their initialization code. Trial loading belongs to goat-inspect. Merging can run on a computer supporting none of the input libraries' target platforms.

## 66. Machine-readable inspection and author information

```sh
goat-inspect geometry.gbin --json
```

In --json mode, standard output contains only JSON, including validation results and detected errors. Exit status keeps its agreed meaning: nonzero on error. Without the flag, output is human-readable. Partial output example (the complete schema remains undecided):

```json
{
  "name": "geometry",
  "version": "1.2.0",
  "validation": "ok",
  "nativeLoad": "ok"
}
```

Add optional author/contact information, including author name and email, to both the .gmod build description and compiled .gbin metadata. The compiler transfers supplied information into the module; missing fields do not prevent building or loading. Inspection should present it as module information, including in JSON mode.

Decisions 67–70 below specify the fields and hash/merge rules. An email field does not imply sending messages or checking the address over the network.

## Informational fields: decisions 67–70

### 67. Agreed set of fields

A .gmod fragment supplementing required fields:

```json
{
  "description": "Geometry computation library",
  "authors": [
    { "name": "Ivan", "email": "ivan@example.com" }
  ],
  "license": "MIT",
  "homepage": "https://example.com/geometry"
}
```

All four fields are optional. Authors permits multiple authors; email may be omitted. Transfer this information to .gbin and display it during inspection.

### 68. Exclude information from the main hash

Description, authors, license, and homepage do not contribute to the main hash. Correcting email or homepage with unchanged code and dependencies does not require rebuilding consumers. Execution and access metadata still contributes to the main hash. This exception covers only explicitly listed informational fields.

### 69. Conflicting information prevents merging

```text
windows.gbin: authors → Ivan, ivan@old.example
linux.gbin:   authors → Ivan, ivan@new.example
```

Even with matching main hashes, goat-merge reports an error identifying the differing field. The author must reconcile metadata before merging; argument order does not select values. A field present in only one input is also a conflict. JSON formatting and object-key ordering differences are not value differences.

### 70. Optional does not permit incorrect types

Omitting email is valid:

```json
{ "authors": [{ "name": "Ivan" }] }
```

The following fragment contains schema errors:

```json
{ "description": 123, "authors": "Ivan", "homepage": null }
```

Present fields must match the schema: description, license, and homepage are strings; authors is an array of objects; name/email values are strings. Do not coerce numbers into strings or interpret null as absence. This applies to .gmod and .gbin metadata.

## Module information strings: decisions 71–75

### 71. Informational strings support Unicode

```json
{
  "description": "Геометрические вычисления",
  "authors": [{ "name": "Иван Князьков" }]
}
```

Read and write JSON as UTF-8. Lowercase ASCII restrictions apply to technical module names and versions, not author names or descriptions.

### 72. Do not perform network validation

```json
{
  "authors": [{ "name": "Иван", "email": "ivan@example.com" }],
  "homepage": "https://example.com/project"
}
```

Building and inspection work offline. Tools neither send email nor open the website or certify the author's claims.

### 73. Preserve string contents exactly

```json
{
  "authors": [{ "name": "Иван Князьков" }],
  "description": "First line\nSecond line"
}
```

Do not trim whitespace or change case. Preserve newlines as part of the value. "Ivan" and " Ivan " differ, including for module merging. JSON canonicalization changes representation, not string contents.

### 74. Each author record requires a nonempty name

```json
{
  "authors": [
    { "name": "Ivan" },
    { "name": "Project team", "email": "team@example.com" }
  ]
}
```

An author may be a person or organization. Email is optional; name is required and cannot be an empty string. Records {}, {"email":"ivan@example.com"}, and {"name":""} are invalid. This does not introduce automatic whitespace trimming contrary to decision 73.

### 75. License is an informational string

```json
{ "license": "MIT" }
```

```json
{ "license": "Proprietary" }
```

```json
{ "license": "Use under a separate agreement" }
```

All examples are valid. No license-catalog match is required. Inspection displays the author's text; the compiler does not interpret license terms or alter program behavior based on them.

## Top-level return and exit: decisions 76–80

### 76. Return outside a function raises an exception

```goat
// settings.goat
public value = 10;
return;
public extra = 20;
```

Top-level return has no function to return from: it raises an exception rather than successfully finishing the file. The proposal for a separate syntactic prohibition was not adopted. An exception escaping initialization makes it unsuccessful under the agreed rules.

Introduce built-in exit(code). During normal execution it immediately terminates the entire interpreter and passes the exit code to the OS. During included-file/module initialization, exit raises an exception instead of terminating the process.

### 77. Initialization context extends through function calls

```goat
// library.goat
public stop = func() { exit(7); };
stop(); // Exception while initializing an included file.
```

The rule follows active execution, not whether exit is syntactically inside a function. A function called by an initializer cannot bypass the termination restriction. Calling the same function during normal execution after successful initialization terminates the process with code 7.

### 78. Direct execution of the main file is normal program execution

```goat
// main.goat, direct source or .gbin entry-file execution
if (__main__) {
    println("Exiting");
    exit(7);
}
```

For exit, top-level execution of a directly launched main file is normal execution, not imported-module initialization. The example therefore exits with code 7. When main connects another file through include/import, its initializer and functions called by it execute in initialization context: exit there raises an exception. This refines earlier references to top-level initialization without changing __main__.

### 79. Normal exit does not run user cleanup

```goat
// Main file, direct execution
try {
    exit(7);
} finally {
    println("Cleanup");
}
```

Terminate at the call with code 7; Cleanup is not printed. Catch cannot intercept normal termination, and user finally blocks do not execute. This is not ordinary exception unwinding.

In initialization context, the same exit raises an ordinary exception and standard catch/finally rules apply. Internal normal-exit cleanup is specified in decisions 82–84 below and does not run user finally blocks.

### 80. Validate the argument before terminating

```goat
exit(0);   // Valid code.
exit(7);   // Valid code.
// The following calls are independent error examples:
exit("7"); // Wrong type.
exit(7.5); // Wrong type.
exit();    // Missing argument.
```

Exit accepts exactly one integer argument within a defined range, without implicit conversion. Check arity, type, and range at the call. An invalid argument raises an ordinary catchable exception, not immediate process termination. This does not mean an uncaught exception is silently ignored. Decision 81 below fixes the inclusive range 0–255.

## Exit codes and shutdown: decisions 81–85

### 81. One range, 0–255, without built-in constants

Exit takes exactly one integer from 0 through 255 inclusive on every platform. Negative values and values above 255 raise an ordinary catchable exception; do not truncate or reduce modulo 256.

```goat
try {
    exit(256);
} catch (e) {
    println("The program continues");
}
```

Do not introduce built-in exit-code constants. Programmers may declare their own. This range does not remove the restriction on exit during included-file initialization.

### 82. Flush standard output buffers

```goat
print("Done");
exit(0);
```

Normal exit flushes standard output buffers before terminating so earlier printing reaches the output even without a newline. This is internal cleanup; user finally blocks still do not run.

### 83. Clean up temporary native libraries

```goat
// The program has already used native functions.
exit(7);
```

The interpreter releases loaded libraries and removes its temporary directory. Cleanup failure produces a warning without changing exit code 7. This refines internal shutdown from decision 79 without permitting user finally execution.

### 84. Output-flush errors do not replace the chosen status

```goat
print("Result");
exit(7);
```

If flushing fails, report it through stderr when possible and retain code 7. An internal shutdown failure does not replace the programmer's explicitly chosen status, just as temporary-file cleanup failure does not.

### 85. Normal exit from a library terminates the whole process

```goat
// Library entry file
external stop = func() { exit(7); };

// main.goat, direct execution
const lib = import "library@1.0.0";
lib.stop();
println("Continuation"); // Not executed.
```

Library has already initialized successfully. Stop terminates the entire interpreter with code 7, not just the library. Active execution context determines behavior: if an active included-file initializer calls the same function, exit raises an exception instead.

## Function analysis metadata: decisions 86–90

These summaries describe proven code properties for the consumer's analyzer. An unknown property does not mean an absent effect. The exact metadata schema remains under design.

### 86. Mutable exports do not fix the initial function

```goat
// Library entry file
external var calculate = func(x) { return x + 1; };

// Program
const lib = import "library@1.0.0";
println(lib.calculate(3)); // 4.
lib.calculate = func(x) { return x * 2; };
println(lib.calculate(3)); // 6.
```

Describing the initial function does not permit permanent binding to it. Execution uses the current field value; analysis and native optimization preserve replacement behavior.

### 87. A constant reference does not imply a pure function

```goat
// Library entry file
var counter = 0;
external next = func() {
    counter += 1;
    return counter;
};

// Program
const lib = import "library@1.0.0";
println(lib.next()); // 1.
println(lib.next()); // 2.
```

Distinguish reference immutability from function properties. Provide detailed summaries of captured-state reads and writes, argument mutation, I/O, possible exceptions, and program termination. Distinguish proven results from unknown properties. External const alone does not establish purity or permit precomputing calls.

### 88. Effects can depend on a callback argument

```goat
// Library entry file
external apply = func(callback, value) {
    return callback(value);
};

// Program
const lib = import "library@1.0.0";
lib.apply(func(x) {
    println(x);
    return x;
}, 5);
```

Metadata records invocation of callback and the dependence of apply's effects on it. A consumer may refine the summary when it can analyze a concrete callback. Otherwise effects remain unknown and the call is not considered pure.

### 89. The result can alias an argument

```goat
// Library entry file
external identity = func(value) { return value; };

// Program
const lib = import "library@1.0.0";
var items = [1, 2];
var result = lib.identity(items);
result.push(3);
println(items); // [1, 2, 3].
```

Support a summary stating that the result is the same object as argument 0. The analyzer must account for shared changes to result and items without library sources. An unproven relationship is unknown, not independence.

### 90. A fresh outer object may share nested values

```goat
// Library entry file
external copy = func(value) { return value.clone(); };

// Program
const lib = import "library@1.0.0";
var items = [[1]];
var result = lib.copy(items);
result.push([2]);  // Does not change the outer items array.
result[0].push(3); // The nested array is shared.
println(items);   // [[1, 3]].
```

A fresh-result summary is insufficient to prove deep independence. Describe proven nested sharing; if exact relationships cannot be established, conservatively retain possible shared state. Crossing a module boundary does not turn clone into deep copying.

## Native specializations and selection guards: decisions 91–95

### 91. List concrete prebuilt specializations

```goat
// Library
external add = func(a, b) { return a + b; };

// Consumer; only a two-integer variant is packaged.
lib.add(2, 3);     // A native specialization applies.
lib.add("a", "b"); // Uses bytecode in ordinary mode.
```

Metadata lists specializations and their applicability conditions. No suitable variant does not make the function unavailable: ordinary mode uses the VM. Do not automatically compile new specializations at runtime; use packaged variants or bytecode.

### 92. Preconditions may be stricter than argument types

```goat
external divide = func(a, b) { return a / b; };
```

If a particular native variant requires a nonzero divisor, divide(10, 2) may use it while divide(10, 0) goes to the VM for ordinary language behavior. Check the condition before entering native code. Arguments have already been evaluated; do not evaluate them again on fallback. Pass the same values to the VM. This does not require every division implementation to have this particular contract.

### 93. Ambiguous selection is a metadata error

```text
Variant A: two integers, nonzero divisor.
Variant B: two integers, positive divisor.
Call divide(10, 2) matches both.
```

Entry order does not resolve the conflict. The compiler must form unambiguous conditions. The loader rejects overlap when it can establish it during validation; remaining ambiguity is checked before the call and is an error. Do not arbitrarily select a variant or hide invalid metadata as an ordinary missing specialization.

### 94. Guards are declarative and side-effect-free

Proposed representation example, not the final schema:

```json
{
  "argumentTypes": ["integer", "integer"],
  "conditions": [
    { "argument": 1, "operation": "notEqual", "value": 0 }
  ]
}
```

Conditions are not executable Goat code. The loader understands a limited, explicitly defined set of checks, such as types and comparisons with constants. They neither invoke user functions nor have side effects. Applicability checks do not change program state.

### 95. Unrepresentable conditions exclude unsafe specializations

```text
Variant requirement: the supplied callback has no side effects.
```

If the requirement is not proven at build time and cannot be checked by supported declarative guards before the call, exclude that specialization. Do not store arbitrary prose expecting the loader to interpret it. The function remains available through bytecode; required follows the previously agreed rules.

## Native-call safety and required: decisions 96–100

### 96. Specialization inapplicability is local to a call

```goat
// Add has a native variant only for two integers.
println(lib.add("a", "b")); // VM: ab.
println(lib.add(2, 3));     // Native: 5.
```

The first call does not disable the function or library for the run. Check specialization conditions on each call. Remembering library loading failure is separate and does not apply to ordinary argument mismatch.

### 97. General call checks precede native selection

```goat
// The function escaped before its source file failed initialization.
savedFunction(2, 3);
```

If the function's source file failed with Error A, the call throws that saved exception even when a two-integer native specialization matches. Validate call eligibility, including source-file state, first; the native path cannot bypass the guard.

### 98. Copying a method does not automatically copy its native binding

```goat
const original = {
    var counter = 0;
    public next = func() {
        counter += 1;
        return counter;
    };
};
const copy = original.clone();
println(copy.next());     // 1.
println(original.next()); // Also 1.
```

Even if the original method has native code, captures redirected by clone/flat must not access the old cells. Execute the transferred method on the VM until native applicability to its new captures and rights is proven. An existing original specialization alone is not such proof.

### 99. A detected ABI violation does not permit replay

```text
Metadata promises integer.
The native function returns an invalid result tag.
```

A detected post-call ABI violation terminates execution with a diagnostic identifying the module, function, and violated requirement. Do not replay the function on the VM: it may already have performed side effects. This covers checkable contract violations and does not promise to intercept arbitrary machine-code faults.

### 100. Required permits VM calls without a specialization

```goat
// Add packages only a two-integer variant.
println(lib.add(2, 3));     // Native.
println(lib.add("a", "b")); // VM, also allowed in required.
```

Required retains the requirement for native execution but does not require every call to be native. Ordinary absence of a specialization for the arguments permits VM execution. If a matching variant exists but its library cannot load, required fails under the previously agreed rules. The module-build requirement of at least one native implementation belonging to the module remains in force.

## Build requirements and archive contents: decisions 101–105

### 101. Requirements reflect the actual native build

If compiler flags enable AVX2, the variant requirements must account for it even when the nominal target is ordinary x86-64. Generate metadata from actual build settings, not just the target name. If the tools cannot reliably determine compatibility requirements, fail the build rather than publish incomplete requirements.

### 102. Flat native-file layout

```text
module.json
bytecode.bin
native/
    variant-1.dll
    variant-2.so
```

module.json contains common metadata and native variant descriptors with library paths. bytecode.bin contains bytecode for all included source files while preserving separate scopes. Native libraries may be absent. Each variant is one library directly under native/, without a per-variant directory, sources, headers, object files, or additional companion DLL/SO files. Names are service identifiers: metadata, not filenames or extensions, determines compatibility.

### 103. Undeclared files are forbidden

If metadata describes native/variant-1.dll but the archive also contains undeclared native/old-library.dll, reject the entire module. Every file must be accounted for by the format or metadata. Directory entries are allowed. Future resources and debug data require explicit format support rather than silently ignoring unknown contents.

### 104. Symbolic links are forbidden

```text
native/variant-1.so -> /usr/local/lib/something.so
```

This entry is invalid: the ZIP contains only regular files and directory entries. A symbolic link invalidates the entire module during validation, even if its variant is irrelevant to the current platform. The library must be present inside the archive. Ordinary system dependencies of the DLL/SO remain resolved by the OS loader under the agreed rules.

### 105. Uniform ZIP path rules

```text
native/variant-1.dll       — valid
native\variant-1.dll       — error
native/../variant-1.dll    — error
```

The only separator is /. Paths are case-sensitive: metadata must exactly match the archive entry name. Nevertheless, names differing only in case are forbidden: native/variant-1.dll and native/Variant-1.dll cannot coexist. This prevents extraction collisions on Windows. Absolute paths and .. components are also forbidden; the rules apply identically on every platform.

## ZIP packaging and validation: decisions 106–110

### 106. Only stored entries and Deflate

Support two storage methods: uncompressed entries and Deflate. Encryption and passwords are forbidden. For example, bytecode.bin may use Deflate while DLL/SO files remain uncompressed. Hashes cover uncompressed data: changing the compression level does not change module identity. An unknown compression method or encrypted entry invalidates the entire module, even when the entry targets another platform.

### 107. No ZIP64 support yet

The first version uses ordinary ZIP. Exceeding its limits fails the build with a clear diagnostic rather than automatically switching to ZIP64. For example, a 5 GiB bytecode.bin requires splitting the program into modules. The loader explicitly rejects ZIP64. This follows the decision not to create huge modules.

### 108. Deterministic packaging

Entry order is fixed, timestamps are constant, and permissions are standard. Build time and original filesystem attributes are not copied into ZIP. Identical module.json, bytecode, and native libraries must produce a byte-for-byte identical .gbin with the same packager version and settings, regardless of the build date or packaging on Windows versus Linux. This is a requirement for packaging identical contents, not a promise that different C compilers produce identical machine code. Content hashes remain independent of packaging.

### 109. Validate sizes before and during decompression

Validate declared entry sizes before decompression and actual sizes while decompressing. If ZIP declares a 1 MiB bytecode.bin but the decompressor produces more, immediately stop processing and reject the module without unbounded memory allocation. An actual size differing from the declared size is an error. Check for overflow when summing sizes. If a correctly declared volume cannot fit in memory, apply the ordinary out-of-memory rule: terminate without trying another module.

### 110. Check CRC32 in addition to SHA-256

Validate CRC32 for every ZIP file entry; a mismatch invalidates the entire module. For example, if native/variant-2.so matches the metadata SHA-256 but has an incorrect CRC32, reject the archive even on Windows where this variant is not needed. CRC32 checks ZIP integrity; SHA-256 checks correspondence between contents and metadata. Neither check by itself establishes trust in the author.

## Further refinement

The rules above are agreed; discussion continues one question at a time with examples. Complete schemas, canonical encoding details, service symbols, and supported system ABIs are refined during implementation design without changing agreed semantics.
