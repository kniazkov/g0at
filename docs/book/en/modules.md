# Modules: Agreed Contract

[Contents](index.md) · [Русский](../ru/modules.md)

Status: design agreed on October 20, 2026; discussion continues. These are requirements for future implementation, not a description of an existing loader. Issues #154 and #169 have been superseded by the [implementation plan](../../module-implementation-checklist.md). Issue #244 fixes exact v1 schemas and reference data before their implementation is completed; #245 owns the shared cleanup service. Where earlier access-control documents disagree, this page takes precedence.

## Build and distribution unit

One compiled module is one file without source code. It represents a library or program intended for independent distribution and repeated reuse. It contains:

- bytecode;
- metadata for imports and binding bytecode to native implementations;
- zero or more native libraries, potentially for multiple platforms.

Execution uses metadata to select a suitable library and implementations. Bytecode is used when no suitable native implementation exists. The loader extracts a library to a separate temporary directory for the run and loads it through standard OS facilities; there is no persistent cache. Prefer simple portable solutions using OS capabilities.

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

Implementation clarification for #179: names are limited to 255 ASCII bytes, versions to 250 ASCII bytes; `com0` and `lpt0`, including extensions, are forbidden. Repeated internal periods and trailing `-`/`_` are allowed. Identifiers are compared exactly, without normalization. Shared `char` and `wchar_t` import parsers return borrowed slices; `string_view_t` remains unchanged. The standalone [book chapter](30-modules.md) documents the implementation incrementally.



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

Use a separate temporary directory per run. Extract libraries on demand and reuse them within the run; no persistent cache yet. A cleanup process started beforehand removes the temporary directory after the Goat process terminates, on both ordinary completion and explicit exit (decision 152). Cleanup failure produces a warning without changing the program's original exit code.

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

### 69. Informational conflicts and author order

After empty-value normalization, differences in description, license and homepage prevent merging even when main hashes match. A nonempty field present in only one input also conflicts. JSON formatting and object-key ordering differences are not value differences.

Authors is checked separately: duplicates are invalid, and two or more nonempty input lists require an explicit human choice of order in goat-merge. See “Author uniqueness and ordering”. This replaces unconditional rejection of every authors difference; differing membership is combined into a duplicate-free union before ordering.

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

Explicit exit attempts to flush stdout and stderr within a shared one-second budget, including lock waits and writes (decision 151). Buffered output may be lost when the deadline expires. This is internal cleanup; user finally blocks still do not run.

### 83. Clean up temporary native libraries

```goat
// The program has already used native functions.
exit(7);
```

A cleanup process started in advance removes the temporary directory containing extracted libraries after process termination (decisions 148 and 152). A library must not be unloaded beneath an executing thread. Cleanup failure does not change exit code 7. This refines internal shutdown from decision 79 without permitting user finally execution.

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

## JSON precision and canonicalization: decisions 111–115

### 111. Read integer fields exactly

```json
{ "formatVersion": 1 }
```

Integer metadata fields use integer JSON numbers without a fractional part or exponent. Values 1.0, 1e0, and the string "1" are invalid for formatVersion. Check ranges without an intermediate conversion to double. Read large integers exactly or reject them when outside the field's allowed range; never round them.

### 112. Strict UTF-8 without a BOM

```json
{ "description": "Геометрия 📐" }
```

Read JSON only as UTF-8 without a BOM. Reject invalid Unicode sequences rather than substituting a replacement character. The same 📐 character may be written as the valid pair \uD83D\uDCD0, yielding the same decoded and canonicalized result. A lone \uD83D is invalid. Apply this rule to .gmod, module.json, and the JSON descriptor inside a native library.

### 113. No automatic Unicode normalization

The character é and an e followed by a separate combining accent may look identical but remain distinct strings. Both are valid in a module description; their difference causes a metadata conflict when merging. Preserve the author's text without hidden normalization. In contrast, "a" and "\u0061" are the same string: decode JSON escapes before comparison and canonicalization.

### 114. Detect duplicate keys after decoding

```json
{
  "name": "geometry",
  "\u006eame": "another"
}
```

This example is syntactically readable as JSON but violates the contract: both keys mean name. Duplicate keys are forbidden at every nesting level, even with identical values. Never silently choose the first or last value. Check after decoding escapes in .gmod, module.json, and the descriptor inside a native library.

### 115. Canonical key order

```json
{ "version": "1.0.0", "name": "geometry" }
```

```json
{ "name": "geometry", "version": "1.0.0" }
```

These objects have the same canonical representation and hash. Sort object keys lexicographically by their decoded UTF-8 bytes, independently of language and OS locale settings. Preserve array element order: ["a", "b"] and ["b", "a"] remain distinct.

## Canonical bytes and bytecode validation: decisions 116–120

### 116. A single canonical string encoding

Canonical JSON writes ordinary characters directly as UTF-8. Escape quotation marks and backslashes as \" and \\, and control characters U+0000–U+001F as \u00xx with lowercase hexadecimal digits. Do not escape /. For example, "a" and "\u0061" become the same canonical "a", while a newline becomes \u000a regardless of its original escape spelling.

The third-party library provides ordinary JSON parsing and escape decoding. Use its serializer for canonicalization only if it matches the contract exactly; otherwise add a small canonical serializer over its data model, not a custom JSON parser. Choosing \u000a rather than \n has no intrinsic advantage: uniqueness and stability, including across library upgrades, are what matter. Source .gmod files may use ordinary JSON escapes and readable formatting.

### 117. JSON formatting is separate from canonicalization

The module.json inside .gbin may be indented. The loader parses JSON, selects the fields participating in the main hash, and canonicalizes them. Whitespace, line breaks, and original key order do not affect that hash. The compiler itself uses stable formatting for byte-for-byte reproducible packaging, but the loader does not demand the same formatting from other tools.

### 118. Main-hash components have unambiguous boundaries

```text
fixed hash-format marker
canonical metadata length
canonical metadata
bytecode length
bytecode
```

Compute SHA-256 over this sequence. Lengths are byte counts encoded as unsigned 64-bit little-endian integers on every platform. The marker separates this hashing format from other SHA-256 uses; its exact bytes must be fixed in the format specification. The significant metadata fields remain unchanged: exclude the hash itself, native variants, and informational fields.

### 119. A native hash covers the entire library file

Compute a native library's SHA-256 directly over every byte of its DLL/SO, without omitting service sections or transforming contents. Changing only an internal DLL timestamp already changes this hash. The module's main hash remains unchanged if bytecode and common significant metadata remain unchanged. This check treats the library as an opaque file; native build settings ensure reproducibility, not hashing exceptions.

### 120. Matching hashes do not replace bytecode validation

Validate bytecode structure and references before executing any user code. For example, a matching SHA-256 does not make an out-of-function jump or nonexistent constant index valid: reject such a module even if the damaged function is unreachable. Validation covers the format, permitted instructions, and their operands; it does not promise to detect all program errors in advance.

## Portability and shared validation: decisions 121–125

### 121. Architecture-independent bytecode

Bytecode inside .gbin uses fixed-width numeric fields and little-endian encoding, without serialized C structures, pointers, or platform-dependent size_t. For example, the same geometry.gbin without native libraries must execute on Windows x64 and Linux ARM64 without rebuilding when the interpreter supports its bytecode version. The loader converts the file representation into its internal representation. This is a format requirement, not a claim that rewriting is necessary: inspect the existing format during implementation and reuse its existing portability properties.

### 122. Cross-check metadata against bytecode

Independent validity of metadata and bytecode is insufficient. If a native specialization references nonexistent function 42, or the entry-file identifier is absent from bytecode, reject the module before execution even when all hashes match. Validate cross-component references.

### 123. Native loading requires an applicable call

If a library contains only add(integer, integer) but the program calls add(string, string), execute on the VM without extracting the DLL/SO or invoking the OS loader. Platform compatibility alone does not trigger loading. File integrity is still checked during initial module validation. goat-inspect separately tests loading the compatible variant as previously agreed. This refines lazy loading without removing preflight validation.

### 124. Native descriptors must agree even with native off

Check specialization lists and library descriptors for consistency before loading a DLL/SO. For example, a specialization referencing variant-3 when only variant-1 and variant-2 exist invalidates the entire module, including with --native off. Disabling native execution does not hide corrupt metadata.

### 125. Shared validation code built through CMake

The loader, goat-inspect, and goat-merge share C code for archive validation. The compiler validates the completed temporary .gbin with it before publication: an invalid function reference fails the build and leaves the old output file untouched. Additional actions differ: the loader validates the dependency graph, goat-inspect attempts compatible native library loading in a child process, and goat-merge does not.

CMake builds the shared validation library and links it into the interpreter and utilities. Installed programs perform validation themselves without requiring CMake. Preserve the existing test invocation mechanism; do not introduce CTest.

## Further refinement

The rules above are agreed; discussion continues one question at a time with examples. Complete schemas, canonical encoding details, service symbols, and supported system ABIs are refined during implementation design without changing agreed semantics.

## Dependency graph validation: decisions 126–131

126. At build time, validate the entire transitive dependency graph, including availability, integrity, versions, expected hashes, conflicts and import cycles. A failure prevents publishing the build.
127. When the same module name/version is required with conflicting main hashes, report both full dependency paths and their expected SHA-256 hashes.
128. Do not impose an arbitrary dependency-depth limit. Traverse the graph iteratively with an explicit work stack, avoiding call-stack exhaustion.
129. Collect all independently discoverable preflight errors in one run. Do not traverse a malformed node whose metadata cannot be parsed.
130. Emit diagnostics in deterministic order: by full dependency chain, then error type and location within a module.
131. Verify each distinct module only once per compiler/loader validation pass and cache its verification result. When a module fails, report every dependency path reaching it. Reuse cached results without hiding distinct paths.

These checks discover dependencies from all syntactic imports, including imports in branches that never execute. Module initialization remains lazy, on actual import execution. Unused dependency declarations remain warnings as specified in decision 56.

## Module lifetime and initialization concurrency: decisions 132–135

132. Module unloading and reclamation of module-owned memory are deferred beyond this iteration. Loaded modules remain resident until program termination.
133. Initialization is blocking, executes on one thread, and is never repeated. Other threads wait for its completion and observe the same scope object or saved failure.
134. Serialize initialization of included source files and imported modules using a global reentrant lock. Direct execution of the main file is excluded from this lock as specified in decision 143. Reentry by the initializing thread supports the already agreed cyclic include behavior. Independent initializations do not run concurrently.
135. Normal function calls into fully initialized modules do not acquire or wait for the global initialization lock. Access that requires initialization or encounters an initializing file follows the established initialization and waiting rules.

## Initialization restrictions: decisions 136–140

136. Attempting to create a thread while initializing a source file or module throws a catchable runtime exception.
137. The thread-creation prohibition applies only to the thread currently executing initialization, not to other threads running ordinary code.
138. Thread join and other blocking thread-wait operations during initialization throw a runtime exception, including waits on previously created threads.
139. Blocking file and network I/O remains permitted during initialization. The global initialization lock stays held; other module initializations must wait.
140. When user mutexes are implemented, blocking acquisition during initialization must throw a runtime exception. Nonblocking tryLock remains permitted. Mutexes are not implemented yet; this is a future requirement.

## Initialization completion: decisions 141–142

141. Initialization has no special timeout. It may run indefinitely, including while performing blocking I/O; termination by external means is outside the language's initialization contract.
142. After failed initialization, repeated include/import throws the exact same exception object with its original stack trace. Neither cloning nor rerunning initialization is permitted.

## Direct execution and the initialization context boundary: decision 143

143. Top-level code of the main file when directly running a .goat or .gbin executes as ordinary program code: without the global initialization lock and without the thread creation and join prohibitions of decisions 136–138. The same applies to functions called by this code when no included/imported file initializer is active.

Actual initialization of a file through include or a module through import acquires the global reentrant lock and enters the initialization context on the current thread. This context covers the entire dynamic initializer invocation, including functions it calls regardless of their declaration file. During this interval, exit, thread creation and blocking thread waits are prohibited; decision 140 applies to future user mutexes. Blocking file and network I/O remains allowed.

On normal completion or exception propagation, restore the previous context. A nested initializer does not remove the outer initializer's restrictions; after leaving the outermost initializer, main code resumes ordinary execution. Repeated include/import of an already initialized file does not create a new initialization context.

This boundary refines decisions 78 and 133–140. It does not remove once-only file execution, scope states, cyclic include rules, or declaration-readiness checks.

## Access to the executing main file: decision 144

144. For a directly executing main file, readiness is checked per declaration rather than by waiting for all top-level code to finish. Already declared functions and initialized variables are available to other threads subject to ordinary access checks. Calling such a function does not wait for the main file to finish. Reading or writing a declaration that is not ready throws a catchable exception rather than blocking.

For example, the main file declares a function, passes it to a new thread, and performs join. The new thread can call that function before the main file's top-level code finishes; waiting for main-file readiness must not deadlock this program. If the function accesses a main-file declaration that has not yet been initialized, the exception occurs at that access.

This exception to waiting in decisions 133 and 135 applies only to the directly executing main file. Other threads still wait for ordinary initialization of included/imported files to finish. Publish declaration readiness and its value with the required inter-thread synchronization; this rule does not make subsequent user updates to variables automatically atomic.

## Required at execution time: decision 145

145. When running a compiled .gbin with --native required, the main module must contain at least one native specialization in a variant compatible with the current platform. Native code in dependencies does not satisfy this requirement. Absence of such a variant or specialization is a preflight error before any user code executes.

Dependencies may contain only bytecode or lack a compatible native variant; this alone does not prevent execution in required mode. Calls without an applicable specialization use the VM. If an applicable specialization in a compatible variant is selected for a call, but its library cannot be loaded or fails exported-symbol/ABI-descriptor validation, required fails. This rule applies to both the main module and dependencies.

The preflight presence check uses validated metadata and does not extract the library or load it through the OS. Extraction and loading remain lazy, at the first applicable call. The program need not actually execute a native call: presence is checked before execution, applicability per call.

The build-time required rule is unchanged: the module being built must obtain at least one native implementation of its own for the selected build target. Compatibility with the execution platform is checked separately.

## Relative include paths and symbolic links: decision 146

146. Resolve a relative include path against the directory of the containing source file's canonical path, after resolving symbolic links. The directory of the link used to find that source file is not the base for nested includes. Apply this rule to every source file, including the entry file.

For example, project/math.goat links to /shared/math.goat. An include "./helpers.goat" expression inside that file selects /shared/helpers.goat, not project/helpers.goat. Canonicalize the included file's path as well to determine its identity within the module. Including the same file directly and through a symbolic link yields the same dependencies, one scope and one initialization.

This rule applies to .gmod builds and direct .goat execution. In a compiled .gbin, relationships between source files are already fixed at build time; source paths and links are not resolved again on the user's machine. Canonical absolute paths are used by the compiler but are not stored in the module in place of logical names.

## Equal threads and VM lifetime: decision 147

147. The VM has no privileged "main thread": all program execution threads are equal. The entry file defines the starting point, but grants no special privileges to the thread executing it and does not make that thread's completion the termination condition for the whole VM.

During ordinary execution, the VM keeps running while at least one program thread has not finished. Completion of the entry file's top-level code finishes that execution but does not stop other threads. Remaining threads can continue using ready entry-file declarations, modules and native libraries. Their shared state is not reclaimed merely because the thread that started the program has finished.

Ordinary VM shutdown begins after all program threads have finished; a separate cleanup process then removes temporary files after process termination under decision 152. This is a VM-wide termination condition, not an implicit join performed by a special main thread.

An otherwise valid explicit exit(code) from any thread still terminates the entire process without waiting for other threads to finish naturally and without executing user finally blocks. The restrictions on exit in a dynamic initialization context remain in force. Decision 147 does not change the rules for fatal VM errors.

## Removing native temporary files after exit: decision 148

148. A separate cleanup process removes the temporary directory after explicit exit. It waits for the Goat process to actually terminate, then removes its temporary directory, including extracted DLL/SO files. Cleanup does not require waiting for program threads inside Goat to finish naturally.

A library must not be manually unloaded while another thread may still execute its code. For this termination path, native-library reclamation is left to the OS and files are removed after process termination. On ordinary completion of all threads, temporary files are also removed by the separate cleanup process after process termination (decision 152).

This refines decision 83: explicit exit does not have to delete loaded DLL/SO files before the Goat process terminates. A removal failure does not change the already selected exit code. Startup timing and readiness acknowledgement are defined in decision 152; startup failure and the shared one-second readiness deadline are defined in decisions 153–154; this decision does not promise successful cleanup under every external failure.

## Exit during native execution: decision 149

149. Exit has the same semantics in the VM and native implementations. Generated native code invokes the shared Goat mechanism through the native ABI instead of independently calling C exit or OS termination and bypassing language checks.

The shared mechanism checks arity, integer type and the 0–255 range, as well as the dynamic initialization context. An invalid argument or prohibited call during initialization raises an ordinary catchable exception, including when called from native code. Ordinary catch/finally rules apply to that exception.

A valid exit terminates the entire process without waiting for active native functions to return, including an infinite loop or a blocking call in another thread. Native functions need not all cooperate by checking a termination flag. User finally blocks are not executed; an in-progress native operation may be interrupted, and its side effects are neither rolled back nor repeated on the VM.

Libraries are not manually unloaded beneath executing code. After actual process termination, the separate cleanup process removes temporary files under decision 148. This rule does not remove the agreed handling of standard output; the flushing budget is defined in decision 151.

## Concurrent exit calls: decision 150

150. Among valid exit calls, the first call that atomically transitions the process into termination fixes the exit code. Other calls neither change that code nor start the termination procedure again. Threads have no priority; a race between exit(0) and exit(7) may produce either code, depending on which call wins the atomic transition.

Arity, type, range and dynamic initialization-context checks precede participation in this transition. An invalid call or prohibited exit from an initializer raises an ordinary exception and does not reserve an exit code. This does not guarantee delivery of that exception if another thread is already terminating the whole process.

Publish the termination state and selected code consistently. Losing calls do not return to user code; termination remains process-wide. A repeated call does not duplicate shutdown actions or dispatch another cleanup job.

## Bounded output flushing on exit: decision 151

151. A valid explicit exit allows a shared one-second budget for flushing stdout and stderr. This is one deadline for both output streams, not one second per stream. Waiting for internal output locks and completion of writes is included. Measure the deadline with a monotonic clock from the start of the flushing procedure; repeated exit calls do not extend it.

If flushing completes sooner, do not wait for the remainder of the second. When the budget expires, terminate the process with the already selected code even if flushing is incomplete; some buffered output may be lost. Emit a warning only if that is possible without additional waiting. An output error or timeout does not replace the exit code.

The bound must remain effective when the flushing thread itself blocks: checking elapsed time only after a blocking write returns is insufficient. This policy refines decisions 82, 84 and 149 and applies to explicit exit; it does not introduce an initialization timeout.

## Cleanup process startup and unified cleanup: decision 152

152. Start one cleanup process for the Goat run's temporary directory before extracting the first native library. Goat must receive acknowledgement that the cleanup process is ready before creating extracted files. Extraction and loading must not proceed before acknowledgement. If neither extraction nor temporary native source compilation (decision 160) is needed, do not start the cleanup process.

The cleanup process waits for actual Goat process termination and removes its temporary directory regardless of the termination path: explicit exit and ordinary completion of all threads use the same mechanism. It does not need an additional request from exit to begin cleanup. After removal or completion of error handling, the cleanup process itself terminates.

Concurrent first native calls must not create multiple cleanup processes for one run or bypass readiness acknowledgement. The cleanup process must not prevent Goat from terminating. Removal failure does not change Goat's exit code.

This refines decisions 83, 147 and 148: the earlier distinction between internal file removal on ordinary completion and external removal on exit is withdrawn. Startup failure and the shared one-second readiness deadline are defined in decisions 153–154. Failure after readiness acknowledgement does not terminate Goat; cleanup is then no longer guaranteed (decision 162).

## Cleanup-process startup failure: decision 153

153. If the cleanup process cannot be started or does not acknowledge readiness, do not extract or load native libraries.

In auto mode, emit one warning explaining the cause, disable native execution for the rest of the entire run, and execute calls on the VM. Subsequent calls neither retry cleanup-process startup nor repeat the warning. This applies to all modules in the run, not only the first module requiring extraction.

In required mode, this failure terminates the program with an error. It is a native-execution preparation failure, not a user-code exception. Side effects already performed before the first applicable call are not rolled back.

Readiness is checked before the first extraction, so no extracted DLL/SO files exist when this failure occurs. This rule does not define the separate policy for failure of a cleanup process that was already running. The readiness deadline is defined in decision 154.

## Cleanup readiness deadline: decision 154

154. Allow one shared deadline of one second for starting the cleanup process and receiving its readiness acknowledgement. Measure it with a monotonic clock from the start of the startup attempt; process creation time is included. This is not a mandatory delay: Goat continues immediately after acknowledgement.

If startup or readiness acknowledgement does not complete within that deadline, preparation has failed and decision 153 applies: auto warns once and uses the VM for the rest of the run; required terminates with an error. No extracted DLL/SO files are created before acknowledgement.

A late acknowledgement does not undo the recorded failure or re-enable native execution. Concurrent calls share one attempt and one deadline without restarting the clock. The cleanup readiness deadline is independent of the separate one-second output-flushing budget on exit.

## Native-library extraction failure: decision 155

155. If a valid native library cannot be extracted from the verified in-memory snapshot to the temporary directory because of a file-operation error, such as insufficient space or denied write access, do not load a partially written file.

In auto mode, report the error once and mark this library unavailable for the remainder of the run. Execute the current call and subsequent calls to its specializations on the VM; do not retry extraction or repeat the diagnostic. Do not automatically disable other libraries. In required mode, extraction failure terminates the program with an error.

Corrupt content or a hash mismatch is not eligible for this VM fallback: it is an unconditional integrity error in both modes. Extraction failure does not change the selected physical module copy or trigger a search in subsequent directories.

Partial files remain covered by temporary-directory cleanup and are not considered successfully published libraries. Concurrent first calls must observe a shared result of the extraction attempt for this library.

## Source identity and hard links: decision 156

156. Within a module, source-file identity is determined by the canonical path after resolving symbolic links, not by the physical file identifier. Paths resolving to the same canonical path refer to one scope, one bytecode copy and one initialization.

Different canonical paths that are hard links are treated as different source files even when their physical identifier and contents match. Do not merge them by inode, file ID or content hash. Each path has its own scope, state and relative-include base.

For example, a/math.goat and b/math.goat are hard links to one file containing include "./helper.goat". The first source includes a/helper.goat; the second includes b/helper.goat. Both sources may belong to the same module as distinct units. Membership in different modules still creates separate entities regardless of an identical canonical path.

## A symbolic link to the build description: decision 157

157. Before resolving build paths, canonicalize the .gmod path itself, resolving symbolic links. Relative main, the local modules search directory and the default modules output directory are relative to the directory of the actual .gmod, not the link's directory. Different ways of reaching the same description do not change the project or its output location.

Example actual project:

```text
/work/project/app.gmod
/work/project/src/main.goat
/work/project/modules/
```

Contents of app.gmod:

```json
{
  "formatVersion": 1,
  "name": "app",
  "version": "1.0.0",
  "main": "src/main.goat"
}
```

The path /home/ivan/app.gmod is a symbolic link to /work/project/app.gmod. The command:

```sh
goat build /home/ivan/app.gmod
```

uses these paths:

| Purpose | Path |
| --- | --- |
| Entry source | /work/project/src/main.goat |
| Local dependency directory | /work/project/modules/ |
| Default build output | /work/project/modules/app/1.0.0.gbin |

The link's location does not make /home/ivan/src/ or /home/ivan/modules/ the build base. Even if those directories contain different files, they do not replace the actual project's files.

An explicit relative --module-dir retains its separate rule: resolve it against the working directory at startup. For example, with /tmp as the working directory:

```sh
goat build /home/ivan/app.gmod --module-dir ./output
```

writes /tmp/output/app/1.0.0.gbin. The entry source remains /work/project/src/main.goat. Dependency search order is /work/project/modules/, then /tmp/output/, then GOAT_MODULE_PATH directories. An explicit --module-dir does not override the priority of the actual project's local modules directory.

This refines main and build-directory resolution without changing how relative --module-dir and GOAT_MODULE_PATH entries are resolved against the startup working directory.

## Running a program through a symbolic link: decision 158

158. When directly running a .gbin or .goat, canonicalize the launched file's path, resolving symbolic links. Derive __dir__ and the local modules search directory from the directory of the actual launched file, not the link.

For example:

```text
/home/ivan/app.gbin -> /opt/myapp/app.gbin
/opt/myapp/modules/
/opt/myapp/config.json
```

The command:

```sh
goat /home/ivan/app.gbin
```

sets __dir__ to /opt/myapp and uses /opt/myapp/modules/ as the first dependency search directory. Locate config.json relative to __dir__. The link's location does not give /home/ivan/modules/ priority.

The same rule applies to direct source execution: if /home/ivan/main.goat links to /opt/myapp/main.goat, __dir__ is /opt/myapp and the local module directory is /opt/myapp/modules/. Resolve relative includes using the actual path of each containing source file under decision 146.

Fix the canonical launch path during startup preparation. A subsequent working-directory change or retargeting of the original symbolic link does not change __dir__ or the local search base. __dir__ remains shared by the entire run, including imported libraries. Explicit relative --module-dir and relative GOAT_MODULE_PATH entries remain relative to the startup working directory.

## Required for direct source execution: decision 159

159. Direct .goat execution with --native required requires at least one native specialization belonging to the launched program itself. If preparing such a specialization fails, user code is not executed. Native implementations in imported libraries do not satisfy this requirement. The program's own code includes sources brought in through include.

For a standalone source, the bound-native-function check already exists: src/cli/native_execution.c requires NATIVE_PREPARE_READY in required mode, and src/codegen/native_pipeline.c produces that status only when bound_functions is nonzero. Module implementation must preserve this behavior and extend ownership accounting, rather than implement the check from scratch.

Current temporary native-build cleanup for direct execution uses reference counting: native_pipeline transfers its workspace to the library, release_native_library closes the loader handle after the last reference and invokes destroy_native_workspace. The latter removes the DLL/SO and directory but does not check removal results. This describes the existing implementation, not completion of the new cleanup-process protocol.

## Cleaning up temporary native source compilation: decision 160

160. The same separate cleanup-process mechanism applies to temporary native compilation during direct .goat execution, not only to libraries extracted from .gbin. Start the cleanup process and acknowledge readiness before creating temporary compilation files and starting the C compiler. The shared one-second startup/readiness deadline from decision 154 applies.

Cleanup covers temporary generated C files, native-build support files, logs, intermediate outputs and the final temporary DLL/SO. After Goat terminates, the cleanup process removes remaining files in its own temporary directory, including after interruption. Ordinary completion of all threads and explicit exit use the same mechanism.

If the cleanup process is not ready, temporary native compilation does not begin: auto warns once and uses the VM for the rest of the run, while required terminates with an error under decision 153. If neither native compilation nor library extraction is needed, no cleanup process is needed.

Explicitly saved output artifacts (--save-library, separate-compilation outputs or distributed-module build outputs) are not temporary runtime files and must not be deleted by the cleanup process. Their publication and preservation on failure remain separate compiler responsibilities.

This extends decision 152: start the cleanup process before whichever first requires temporary native-compilation files or library extraction. Existing reference-counted cleanup does not replace the external cleanup process. Decision 161 defines the ordering when a child C compiler is still running after Goat terminates.

## Goat interruption during native compilation: decision 161

161. During ordinary preparation for direct execution, Goat waits for the C compiler to finish before executing user code. Therefore user exit cannot interrupt this initial compilation. The following rule concerns external interruption or abnormal termination of Goat.

If the C compiler launched for that run's temporary native build, or its child processes, are still running after Goat terminates, the cleanup process first stops that process group and waits for it to terminate, then removes the temporary directory. Removal must not race with continuing compiler writes.

Stop only processes belonging to the particular temporary build of that Goat instance. Do not identify processes by compiler executable name or affect other builds. Ownership tracking and handoff to the cleanup process must cover compiler startup and descendants, including Goat interruption during startup itself. Explicitly saved user build artifacts remain excluded from cleanup.

## Failure of a ready cleanup process: decision 162

162. If the cleanup process acknowledges readiness and subsequently terminates abnormally, Goat continues execution. This failure alone neither terminates the user program nor changes its exit code; this also applies to native required mode.

Temporary-file removal is no longer guaranteed after this failure. Stopping a remaining compiler and its descendants before removal likewise requires a functioning cleanup process. Do not remove the directory or unload DLL/SO libraries beneath running code to compensate for the failure.

This differs from failure before readiness acknowledgement, which remains governed by decisions 153–154. Previously acknowledged readiness must not be retroactively treated as a startup failure, and the cleanup startup failure policy must not be applied to the program.

## .gmod v1: required fields and defaults

Only `formatVersion`, `name`, `version` and `main` are required. In v1, `formatVersion` is the integer 1; the other three fields are strings subject to their respective validation rules.

| Field | Type | When omitted |
| --- | --- | --- |
| dependencies | object mapping module names to version strings | Empty object: no default versions |
| native | object | mode = "auto", target = "host" |
| native.mode | string: off, auto or required | "auto" |
| native.target | string | "host" |

`native.mode` and `native.target` may be omitted independently. An empty `native: {}` is valid and applies both defaults. An empty `dependencies: {}` is valid. This table covers optional settings, not the complete field list: the agreed informational fields are also optional.

Explicit `null` in place of any listed object or value is invalid and does not request a default. Omitting any required field is an error.

Minimal description:

```json
{
  "formatVersion": 1,
  "name": "geometry",
  "version": "1.2.0",
  "main": "src/main.goat"
}
```

Checks: accept the minimal description; reject omission of each required field; apply the stated defaults to omitted and empty dependencies/native; allow either mode or target to be omitted independently; reject null in each listed field.

## .gmod v1: the main path

The `main` field accepts a relative or absolute path to the entry source file, including a file outside the project directory. Resolve a relative path against the directory of the real .gmod after resolving symbolic links; do not prepend that directory to an absolute path. Canonicalize the resulting source path using the common file identity rules.

For example, `"main": "../shared/main.goat"` is valid. Relative paths are recommended for portable projects; accepting absolute paths does not make them portable across machines or operating systems. The build machine's absolute path is not stored in .gbin.

Checks: relative main inside and outside the project; absolute main outside the project; identical resolution of relative main when opening .gmod directly or through a symbolic link; changing the working directory does not change the selected source.

### Literal main value

After JSON decoding, the `main` string is interpreted as a literal path. Environment-variable substitution and `~` expansion are not performed. For example, `"$HOME/main.goat"` is a relative path containing a literal `$HOME` directory, and `"~/main.goat"` contains a literal `~` directory. Neither `${HOME}` nor `%USERPROFILE%` triggers substitution. Normal JSON escape decoding and the agreed path resolution and canonicalization rules still apply.

Checks: changing an environment variable does not change main resolution; a path containing `$HOME` or `~` selects the corresponding literal directory beside the real .gmod when that file exists. If the literal path does not exist, report the ordinary missing-source error without retrying with substitution.

### Separators in main

After JSON decoding, `/` is supported as a directory separator on every supported OS. Backslash follows OS rules: it is a separator on Windows and an ordinary filename character on Linux. Do not unconditionally replace backslashes with forward slashes on every platform. Portable descriptions should use `/`.

The JSON example `"main": "src/main.goat"` uses a portable separator. In `"main": "src\\main.goat"`, JSON escaping produces one backslash: on Windows this selects main.goat inside src; on Linux it denotes a filename containing a backslash.

Checks: the `/` form resolves on Windows and Linux; the escaped backslash form selects the nested file on Windows and the literal backslash filename on Linux, without substitution of a path containing `/`.

### Incomplete Windows paths in main

On Windows, reject paths relative to a drive's current directory (for example, `C:main.goat`) and paths rooted on the current drive without specifying that drive (for example, `\src\main.goat` or `/src/main.goat`). Do not resolve these paths using process state or prepend the .gmod directory.

An ordinary relative path such as `src/main.goat` and a fully qualified absolute path such as `C:/src/main.goat` remain valid. These examples show values after JSON decoding; backslashes must be escaped inside JSON. This restriction concerns Windows path interpretation: on Linux, `/src/main.goat` remains a fully qualified absolute path.

Windows checks: reject `C:main.goat`, `C:`, `\src\main.goat` and `/src/main.goat` regardless of the current drive and its working directory; retain support for ordinary relative paths and fully qualified drive paths. On Linux, verify that an absolute path starting with `/` is not rejected by the Windows-specific rule.

### Empty values, NUL and spaces in main

After JSON decoding, reject an empty string and any string containing U+0000 (including one written as `\u0000`) before accessing the file system. Do not pass a path containing NUL to a C API that silently truncates it.

Preserve leading, trailing and internal spaces without automatic trimming. For example, `"main": " main.goat "` supplies a path with leading and trailing spaces; a string consisting only of spaces is not empty. Such filenames are possible on Linux. Support for a particular name depends on the OS, file system and file API; ordinary Windows interfaces have restrictions and normalization behavior concerning trailing spaces. The format rule prohibits trimming by Goat's loader itself, but does not promise name portability or changes to OS rules.

Checks: reject an empty string and NUL at the beginning, middle and end; verify preservation of spaces after JSON decoding. On Linux, create distinct files named `main.goat` and ` main.goat ` and verify that the specified file is selected. Report ordinary resolution, missing-source or read errors without an additional attempt using a trimmed name.

### Entry source extension in main

The `.goat` extension is recommended but not required for the file specified by `main`. Read that file as Goat source text regardless of its extension, including no extension. Do not append anything to the path or search for another file with a `.goat` suffix.

For example, `"main": "src/start"` selects exactly `src/start`, even if `src/start.goat` also exists. Read or syntax errors in the selected source do not trigger a search for an alternative name. This rule concerns main in .gmod and does not redefine mode selection for an arbitrary command-line argument.

Checks: successfully build valid source with the `.goat` extension, another extension and no extension; when both `start` and `start.goat` exist, select the explicitly named file; report an error if `start` is missing even when `start.goat` exists; diagnose unsuitable content as a source error.

### File type of main

After resolving symbolic links, main must refer to a regular file. A symbolic link to a regular file is allowed. Reject directories, devices, named pipes, sockets and other special objects with a diagnostic before reading their contents as source. Failure to determine the type is not confirmation of a regular file.

The check applies to the object actually opened; replacing the object between path checking and opening must not allow a special object to be read. The implementation must not block while opening a named pipe merely to inspect its type. Access and read errors on a regular file remain build errors.

Acceptance: accept a regular file and a link to it; reject a directory, a link to a directory and special objects available on the test OS. The named-pipe test needs no external writer and completes without waiting for data; an external test timeout only detects hangs.


## Empty informational fields: normalization

In .gmod and module.json, empty strings in `description`, `license`, `homepage` and `authors[].email` are equivalent to the respective field being absent. An empty `authors: []` is equivalent to absent authors. After type validation, remove these empty values from the internal representation; the compiler and goat-merge omit them from the output .gbin. Readers accept explicitly stored empty values and normalize them to the same representation.

Every author entry still requires a nonempty name: `authors: [{"name": ""}]` is an error, not an entry to discard. `null` remains a type error in all listed fields. Do not trim spaces: `" "` is nonempty and is preserved. Preserve all nonempty strings exactly, without case changes or Unicode normalization. This rule does not apply to technical name/version/main fields, dependencies or native settings.

Goat-merge compares informational data after this normalization. An absent field and its empty value do not conflict; a nonempty description/license/homepage field present in only one input still causes a conflict; authors differences follow “Author uniqueness and ordering”. For example, `{"description":"","authors":[]}` and `{}` are equivalent, as are `{"authors":[{"name":"Ivan","email":""}]}` and `{"authors":[{"name":"Ivan"}]}`. Normalizing informational data does not change the main hash.

Acceptance: compare each empty field with its absence when reading .gmod/module.json and during merge; output .gbin omits these empty fields. Test equivalence of empty and absent email, rejection of empty name and null, preservation of a spaces-only string, and conflict between a nonempty value and absence. Merge output must not depend on input order for equivalent empty/absent data.

### Email and homepage are informational text

Goat does not validate the syntax of `authors[].email` or `homepage`: email-address validity, URL format, protocol scheme or domain name. Any nonempty JSON string is accepted in these fields and preserved exactly; an empty string normalizes to absence under the general rule. Type and JSON/UTF-8 validity checks still apply; null is not allowed.

The compiler, loader, goat-inspect and goat-merge perform no network checks and do not reject a module because of the contents of these informational strings. A possible future Goat artifact website may impose its own publication checks; these are not requirements of the format or current tools.

Acceptance: accept strings such as `email: "contact through the website"` and `homepage: "page not created yet"`, transfer them and display them as text; continue rejecting invalid types. Verify exact preservation of nonempty values, normalization of empty values and absence of network requests.


## Author uniqueness and ordering

Authors is an ordered list without duplicates. An entry is identified by the `name + email` pair after JSON decoding and normalization of empty email to absence. Compare exactly, without trimming, case changes or Unicode normalization. Missing and empty email represent the same value. Identical names with different emails are distinct entries; identical emails with different names are likewise not automatically combined.

A repeated pair within authors makes .gmod or module.json invalid; silent deduplication is prohibited. This includes pairs that become equal after empty-email normalization. Preserve author order during compilation and reading.

When merging, goat-merge first normalizes empty and absent authors to empty lists and considers all input archives together. If every list is empty, omit authors from the result without prompting. If exactly one list is nonempty, copy it with its order preserved without prompting. This is the only case for automatically selecting a nonempty order.

If two or more lists are nonempty, human intervention is required even when their membership and order are identical. Form the union of all authors by the name + email pair, show every entry with name and email, and ask the person to select the first author, second author, and so on. Every author in the union must occur exactly once in the result; do not automatically exclude authors. An entry shared by separate archives occurs once in the union; duplicates within any individual input list remain invalid.

Example: [Ivan, Anna] and [Anna, Peter] produce candidates Ivan, Anna, Peter whose order is chosen by the person. [] and [Anna, Ivan] automatically produce [Anna, Ivan]. With three or more inputs, pairwise intermediate merging must not bypass the selection requirement: two nonempty input lists require human intervention.

Input archive order and automatic sorting do not determine final order. Do not publish the output archive or replace an existing output file until selection is complete. Conflict rules for description/license/homepage remain unchanged. When interactive input is unavailable, required selection fails as specified below.

Acceptance: reject duplicate pairs within an input list; allow identical names with different emails; do not prompt when all lists are empty; copy a sole nonempty list without prompting; require selection for two nonempty lists with identical, reordered or different membership. Test three inputs including two nonempty lists. The result contains the entire union in the chosen order without duplicates or omissions. Keep any existing output file unchanged until selection is complete.

### Author selection without interactive input

If the merge rules require author-order selection but interactive input is unavailable (for example, in CI), goat-merge exits with a nonzero status and a diagnostic explaining that interactive selection is required. It must not choose an order automatically or hang waiting for input. Do not publish a new output archive; preserve any existing output file unchanged.

When all input lists are empty or exactly one list is nonempty, no selection is required: unavailable interactive input alone does not prevent merging. All other archive and compatibility checks still apply.

Acceptance: with interactive input unavailable, two nonempty lists (including identical lists) produce a diagnosed failure without changing existing output; all-empty lists or a sole nonempty list pass author processing without prompting.

## Dependencies in module.json: an array of exact records

In compiled .gbin metadata, `dependencies` is an array of objects, each containing the required string fields `name`, `version` and `hash`. Name identifies the dependency, version is its exact version, and hash is its expected main SHA-256 as 64 lowercase hexadecimal characters. Unknown fields are prohibited; null and other value types are invalid.

Record-shape example (the zero hash below illustrates syntax only; real dependencies store their computed hashes):

```json
{
  "dependencies": [
    {"name": "math", "version": "1.0", "hash": "0000000000000000000000000000000000000000000000000000000000000000"},
    {"name": "math", "version": "2.0", "hash": "0000000000000000000000000000000000000000000000000000000000000000"}
  ]
}
```

Different versions of the same name are valid separate records. Repeating a name + version pair is invalid even with the same hash; different hashes are also an error, not a choice between records. Repeated source imports of one dependency produce one record during compilation. Common name and version restrictions apply; ranges and latest-version selection are not supported.

In .gmod, dependencies retains its different form: an object mapping names to default versions, without hashes. These are import-resolution settings, not the serialized actual dependency list of a compiled module. The compiler writes actual dependencies and their verified main hashes into .gbin.

Acceptance: accept two versions of math; reject a repeated pair with either matching or conflicting hashes; reject missing record fields, invalid types, unknown fields and invalid hash spelling; repeated imports of one pair do not duplicate its record. Verify that the existing .gmod dependencies form remains valid and that substituting either representation into the other format is rejected.

## Default versions and unordered dependencies

In .gmod, dependencies provides at most one default version per name: an object mapping names to exact version strings. An array of versions in place of a string is invalid; a repeated name key is a JSON error. Using multiple versions of one module does not require multiple descriptor entries.

With `"dependencies": {"math": "2.0"}`, `import "math"` resolves to math@2.0, while `import "math@1.0"` explicitly selects math@1.0 independently of the default. If both imports are used, the compiled .gbin records both versions with their main hashes. An explicit import does not change versionless import resolution. Without a default for the name, a versionless import is a compile error: do not infer its version from other imports, available files or dependencies of compiled libraries.

Dependencies have no semantic order. Reordering .gmod keys or module.json dependency records does not change dependency membership, import resolution or main-build identity. This is neither a priority list nor initialization order. The technical canonical hash representation must respect these set semantics; general JSON array-order preservation does not make dependency order meaningful. Author order remains meaningful.

Goat-merge combines native variants of one main build with dependencies already fixed. It does not reselect versions or combine incompatible dependency sets into a new main build; rearrangement of identical records is not a conflict.

Acceptance: test joint use of math@2.0 through versionless import and math@1.0 through explicit import; reject versionless import without a default even if another location contains exactly one explicit import of that name. Reject a version array in .gmod. Reordering dependencies must not alter resolution, main hash or merge eligibility; reordering authors retains its separately agreed rules.

## Direct dependencies and the complete graph

Each compiled .gbin dependencies list records only that module's direct dependencies: imports from its entry file and all its own source files included through include. Syntactic imports in unexecuted branches or uncalled functions are included under the common rule. Dependencies of imported modules are not copied into this list.

For example, if A imports B and B imports C, A metadata records B with its version and main hash, while B metadata records C with its version and main hash. C appears in A dependencies only when A's own sources import C. A transitive dependency does not replace that import or provide A with a default version.

During compilation and execution preparation, validation traverses the entire reachable graph: locate direct dependencies, validate their archives and read their own dependencies until every reachable module is resolved. The root archive needs no flattened complete list. Validation precedes user code and invokes neither initializers nor the OS loader. The same search directories apply throughout the pass.

Traversal is iterative rather than recursive by dependency depth; validate each shared node once per pass while retaining all paths leading to it for diagnostics. Distinguish a fully validated shared node from a back edge on the active chain: a dependency diamond is valid, an import cycle is an error. Cycle detection remains mandatory for input archives even though a correct compiler must not produce cyclic graphs.

Traversal succeeds for a finite, available, valid graph. Absence of cycles alone does not guarantee success: a missing module, access error, corruption, unsupported format, version/expected-hash conflict or another validation error prevents execution/build publication under the common rules.

Acceptance: A → B → C resolves and validates C without listing C in A; A's own import of C adds a direct record, including imports from A's included sources. A diamond validates the shared node once while retaining diagnostic paths. A deep chain does not overflow the stack. Detect a cycle or a missing/corrupt transitive C before any user side effect. An unexecuted import branch does not hide a dependency from preflight.

## Empty dependencies of a compiled module

The dependencies field in module.json is optional. An absent field and `"dependencies": []` represent the same empty set of direct dependencies. Readers accept both; the compiler and goat-merge omit the empty field when writing .gbin. Null, an object or any other non-array type is invalid.

Before producing canonical metadata for the main SHA-256, normalize empty dependencies to an absent field. Adding or removing an explicitly empty array therefore does not change the main hash when all other significant data is unchanged. Apply ordinary JSON canonicalization to this normalized representation. Nonempty dependencies remain part of the main hash, including each direct module's name, exact version and expected hash.

Goat-merge does not treat an absent field and an empty array as different main builds. Normalization does not bypass metadata/bytecode import consistency checks: absent or empty dependencies is valid only when there are no direct external imports.

Acceptance: accept both empty forms for a module without external imports; produce identical canonical metadata and main hashes and allow merge; omit the field in written output. Reject null/invalid types. Reject an empty list in the presence of an external import as metadata/bytecode inconsistency, even if the hash has been recomputed.

## Complete .gmod validation before overrides

The .gmod format is strict. First validate JSON and the entire descriptor schema, then normalize permitted empty values and apply defaults and command-line overrides. Unknown fields at any level, duplicate decoded keys, invalid types, missing required fields and values outside the agreed constraints are errors. An override does not repair an invalid descriptor.

For example, `"native": {"mode": "atuo"}` is rejected even with `--native off`. An invalid target value or type is not hidden by --native-target. Validate unused dependencies entries too: an invalid name or version is an error even without a corresponding import. A valid unused entry still only produces the previously agreed warning; its archive need not be located or loaded.

Format validation does not execute disabled operations: native off need not locate a C compiler or build native code. A syntactically valid explicit target is checked for actual build availability only if used after overrides. Informational email/homepage remain arbitrary strings under the agreed contract: strict schemas do not introduce email-address or URL validation.

Acceptance: reject a misspelled mode despite an override, an invalid type in an overridden target, unknown nested fields and invalid unused dependency entries. Accept a valid descriptor with a valid override. Verify no native build attempt in off and no search for a valid unused dependency. Detect all descriptor errors before publishing build output.

## Discovering available native targets

`goat --list-native-targets` derives its list from compilers actually discovered on the system and their verified capabilities. It does not advertise a large static architecture catalog as available. For every available target, show its identifier, a readable description and the specific compiler that passed the check. A target identifier describes OS, architecture and system ABI, for example `linux-x86_64-gnu` or `linux-x86_64-musl`; `host` denotes the current machine's target. These examples alone do not declare those targets available.

Before listing a target, compile and link a small dynamic library DLL/SO in a temporary directory. Merely finding a compiler executable or successfully querying its version is insufficient: the build components, including headers, libraries and the linker needed for the probe, must work. The output artifact must match the tested target. Do not execute or OS-load the resulting library: its platform may differ from the current one.

List only targets that pass this check. A failed probe must not imply availability; explicitly report when no target succeeds. A successful minimal probe confirms basic DLL/SO build availability, not successful compilation of every future generated program.

Use the shared temporary native build and cleanup infrastructure #245. Responsibilities: #211 discovers compilers, determines candidate targets and performs probes; #191 exposes the CLI option and presents results. Syntactic target-identifier validity and availability in the current environment are separate checks: a missing compiler does not make a valid .gmod schema-invalid. Check actual build availability according to native mode and explicit-target rules.

Acceptance: a discovered compiler with a successful probe yields a target and compiler details; one that reports its version but cannot link DLL/SO does not yield an available target; successful cross-compilation does not require executing the result. Check that no OS loading occurs, temporary files are cleaned, and absence of available targets is clearly reported. Use controlled compiler stubs for automated tests and available real toolchains for integration checks.

Discovery follows “Compiler discovery and --cc”. Automatic selection and probe deadlines are defined in the corresponding sections below.

## Compiler discovery and --cc

The Goat process environment is the primary configuration mechanism for native compilation. Without an explicit selection, Goat discovers compilers through PATH and launches them with the inherited environment, including tool, header and library paths configured by the user or installer. Do not scan the entire disk, installation directories or separate machine-specific stores. Users must prepare a working toolchain environment for native compilation.

The optional `--cc <compiler-path>` selects one compiler explicitly. In this mode Goat uses only that compiler; if it is missing, cannot start or cannot build the selected target, do not substitute another compiler from PATH. Apply the agreed diagnostics and native-mode policies; prohibiting compiler substitution does not itself remove VM/bytecode fallback allowed by those policies. The selected compiler also inherits the environment.

These rules also apply to `goat --list-native-targets`: without --cc inspect compilers from PATH; with --cc inspect only the selected compiler. Do not store machine-specific compiler paths in .gmod. Do not introduce additional user-maintained compiler-search directory lists.

Acceptance: discover a compiler on configured PATH; do not find tools outside PATH through hidden scans; environment variables reach both probe and actual builds. With --cc, invoke only the selected compiler; a controlled failure must not launch a fallback compiler from PATH. Target listing with --cc reflects only successful probes of that compiler.

## Automatic native compiler selection

Without --cc, Goat automatically selects a compiler suitable for the requested target. Multiple suitable compilers are neither an error nor a reason to prompt. Running a small program must not require interactive toolchain selection. The optional --cc remains an explicit selection and prohibits substitution.

Automatic selection order must be stable and documented rather than dependent on parallel probe completion times or arbitrary file enumeration. Reuse the existing initial preference: cc on Linux and gcc on Windows, resolved through PATH. If the preferred compiler is unavailable or fails the requested-target probe, automatically consider other discovered supported compilers. Define the exact remaining candidate order in the discovery implementation and its documentation.

Do not conflate a failed candidate probe with a user-program compilation error. After selecting a suitable compiler, failure to build generated C follows the previously agreed rules and is not hidden by trying other compilers or silently falling back to bytecode. If no suitable candidate exists, apply the agreed native auto/required and explicit-target policies.

In --list-native-targets, show verified compilers suitable for a target and identify the automatic choice. Multiple compilers for one target do not create distinct platform identifiers solely because their compiler names differ.

Acceptance: two suitable compilers cause neither a prompt nor an ambiguity error; selection is reproducible with an unchanged environment. An unsuitable preferred candidate allows selection of another; explicit --cc disallows substitution. A user-C build failure with the selected compiler does not trigger a hidden search through other compilers.

## Stop compiler discovery after the first suitable candidate

During ordinary execution or compilation, Goat checks candidates in the agreed stable order and stops immediately after the first compiler successfully passes the requested-target probe. Do not probe remaining candidates or unrelated targets merely to build a complete inventory. If the first candidate is unsuitable, check the next one; do not prompt.

The --list-native-targets command deliberately examines all discovered supported candidates and their candidate targets, presenting every successfully verified option rather than only the first. A failed candidate probe does not stop examination of the remaining candidates. The --cc restriction remains: both operations consider only the explicitly selected compiler when specified.

Stopping at the first successful candidate does not mean selecting the fastest process to respond. Ordinary execution and full listing use the same selection priority with an unchanged environment. The listing marks the same automatic choice for the requested target that ordinary execution would make.

Acceptance: a controlled first suitable compiler passes its probe while a second compiler counts invocations; ordinary compilation does not invoke the second. If the first fails its probe, check the second. With --list-native-targets, check both and display all successful results. Explicit --cc never invokes other compilers in either mode.

## Native compiler probe timeout

A compiler probe for a candidate target has a default timeout of 10 seconds. `--native-probe-timeout <seconds>` accepts a positive integer number of seconds overriding this default, for both ordinary discovery and --list-native-targets. Zero, negative, fractional or unrepresentable values are option errors; this option does not enable unlimited waiting.

Measure the deadline using monotonic time. One probe has a shared budget for launching and executing its commands, including compilation and linking, rather than a fresh full timeout for each phase. External compiler-information queries must also be bounded by this configurable timeout and cannot hang indefinitely before the build probe. This timeout checks compiler availability; it does not limit compilation of user-generated C.

When the deadline expires, mark the probe unsuccessful. Stop its processes and their owned descendants, and wait for writes to cease before cleaning temporary files. Do not affect unrelated builds. Ordinary automatic discovery continues with the next candidate; full listing continues examining the remaining candidates. With --cc, do not select another compiler. Success arriving after expiry does not undo the recorded timeout.

Acceptance: stop a controlled hung probe, do not advertise its target as available, and allow the next candidate to proceed; explicit --cc prevents substitution. Test hangs in information queries and linking, the configured timeout, invalid option values, stopping owned descendants before cleanup, and absence of this timeout on user builds. Tests use a short explicit timeout, startup acknowledgement and an external fallback timeout in the test harness.

## Native compiler discovery diagnostics

During ordinary execution or compilation, do not print rejected-candidate errors if a suitable compiler is subsequently found. Retain failed-probe reasons until selection completes so that, when no suitable candidate exists, a clear summary identifies each tested compiler and target and the failure reason, including launch, compilation/linking errors or timeout. If no candidate was discovered, state that explicitly rather than showing an empty summary.

Reporting reasons does not change native auto/required or explicit-target policy: apply the previously agreed bytecode-fallback or failure rules after diagnostics. With --cc, reasons concern only the selected compiler; no hidden substitution occurs. Failures to compile actual user-generated C are not rejected-candidate errors and are not hidden by this rule.

In --list-native-targets, always show successfully verified available targets and a separate failed-checks section if any checks failed. Do not include a failed target among available targets. Associate each failure with its compiler and target; retain successful results from other candidates.

Acceptance: the first candidate fails its probe and the second passes; ordinary execution does not print the first error. If all candidates fail, show reasons for every performed check; report absence of candidates separately. Listing shows a successful target and a separate failure from another candidate. Clearly distinguish timeout from compilation failure; user-build errors remain visible.

## Exit status of --list-native-targets

When enumeration completes normally, return 0 if at least one target passed the build probe. Failures of other candidates, including timeouts, appear in the separate diagnostics section and do not make the final status nonzero when a successful target exists.

Return a nonzero status if no target was successfully verified. This covers both no discovered compilers and failure of every performed probe. With --cc, consider only results from the explicitly selected compiler. Invalid options and command-level errors are not converted into success by this rule.

Acceptance: one successful probe yields 0; success together with another candidate's failure or timeout also yields 0 while retaining diagnostics; no compilers or all failed probes yield a nonzero status. Scripts can use the status to check for at least one available native target without parsing human-readable output.

## Compiler probe result cache

Native-target availability results are retained only in memory for the current Goat invocation. Repeating the same probe under unchanged conditions reuses its result instead of compiling and linking again. Success, failure and timeout belong to a specific compiler, target and probe configuration; do not reuse a result for another target or configuration.

Do not persist results across invocations or restore them from a persistent cache. A new invocation probes the required candidates again because the compiler, environment, headers, libraries or linker may have changed. Caching does not require probing unnecessary targets: ordinary discovery still stops at the first suitable compiler, while --list-native-targets performs full enumeration within discovery scope.

Caching a result does not require retaining its probe DLL/SO for a future invocation. Temporary files remain managed by the shared cleanup service. These rules concern compiler availability checks and do not introduce a cache of user-program native code.

Acceptance: an identical repeated probe within one invocation does not invoke the compiler again; a different target or configuration needs its own check. Two separate invocations probe again. Changed compiler availability between invocations affects the new result; success retained by the previous process is not reused.

## Entry file in shared bytecode

A .gbin still contains one bytecode.bin for the entire module. The main field in module.json is a numeric identifier of a logical source file within that module, not a path to a separate binary or an instruction address itself. In .gmod, main remains a source-path string.

The file table associates the identifier with a logical diagnostic name and the entry point of its top-level code in the shared instruction section. Each source file retains its own scope, access rights and initialization state; shared bytecode does not merge them. Executing a compiled module does not require source files on disk.

The current VM indexes code->instructions using thread->instr_id; newly allocated threads are zero-initialized and start at instruction 0. This is more precise than “byte zero”: current execution addresses are instruction indices. Preserve the ordinary entry point 0 for a single source. In the multifile format, entry-file identity and its code address are distinct concepts; execution uses their validated association in the file table.

Before execution, verify that main exists, the file table agrees with bytecode, and the entry point is valid. Direct execution runs the main file's top-level code as an ordinary program, while import uses that same entry file for one-time initialization of the imported module. Other files initialize through include rather than their physical code order. Preserve the agreed dynamic-context distinction between direct execution and include/import.

Acceptance: a single source still starts at instruction 0; a multifile module contains one bytecode.bin with separate scopes and entry points; invalid main identifiers and entry points are rejected before user code. Relocating code while correctly updating references does not change the selected logical entry file. Execution does not access source files on disk.

## The files table and entry addresses

In module.json, the required files field is a nonempty array describing logical source files. Each record contains required fields name (a string used as the logical diagnostic name) and entry (a nonnegative integer instruction index for the start of the file's top-level code in the shared bytecode.bin). The boolean external field described below is also required. This is a closed object: there is no separate id field and unknown fields are prohibited. Null, invalid types, and fractional or exponential spellings of integer fields are rejected under the common rules.

A file identifier is its array index: 0, 1, 2, and so on. It is local to the module; identical indices in different modules do not merge their files, scopes or rights. The required main field in module.json is a nonnegative integer selecting a files record and must be less than the record count. In .gmod, main remains a source-path string; do not confuse these formats.

Example module.json fragment; other metadata is omitted and instruction addresses are illustrative:

```json
{
  "formatVersion": 1,
  "name": "geometry",
  "version": "1.0.0",
  "main": 0,
  "files": [
    {"name": "src/main.goat", "entry": 0, "external": false},
    {"name": "src/vector.goat", "entry": 40, "external": false},
    {"name": "src/matrix.goat", "entry": 120, "external": false}
  ]
}
```

Main = 0 selects src/main.goat with entry point 0. Bytecode include of vector.goat references local file 1. The VM uses that file's separate scope and initialization state; if first execution is required, it starts at instruction 40. The name path is not opened on disk during compiled-module execution.

The validator checks every file reference and entry against the shared instruction section and bytecode descriptions, including correspondence of the entry point to that file's top-level code. Entry values are instruction indices, not byte offsets. Never silently replace an invalid main or entry with zero.

The files order matters for addressing: reordering records changes identifiers and requires consistent updates to main, include and every other file reference. Do not automatically sort this table during reading or canonicalization as though it were unordered dependencies. The compiler assigns deterministic indices before writing all related structures.

Acceptance: the example correctly associates main and include with separate files in one bytecode.bin; identical indices in two modules remain independent. Reject empty/missing files, out-of-range main, include referencing a missing file, invalid entry, a separate id field and invalid types before execution. Consistently changing the table and all references still selects the same logical files.

### Internal and external file-name uniqueness

For records with external = false, files[].name values must be unique within a module. Two internal records sharing a name make module.json invalid regardless of entry. Compare decoded strings exactly, without case conversion, trimming or Unicode normalization; alternative JSON escaping of one string does not create distinct names. Compare names as logical labels, not using the executing machine's file-system rules.

For records with external = true, identical name values are allowed: external sources are distinguished by their files indices. An internal and an external file may also share a name. Runtime identity is the module and file index, not the name. Sources with different canonical paths are not combined because their names match.

External-file diagnostics show the name, external status and index, for example: math.goat (external file #1). Preserve module context. Identical logical names in different modules are allowed.

Acceptance: reject duplicate names among internal records, including equivalent JSON escaping; accept identically named external files and matching internal/external names. Diagnostics distinguish external files by index; their scopes and states remain independent.

## Deterministic source-file numbering

During compilation, assign identifier 0 to the entry source. Then traverse include edges depth-first: examine includes in source-text order, assign the next files index to each newly reached source, and traverse its includes before continuing the current file. Source position determines order, including includes in nested functions and unexecuted branches; do not execute user code to discover this order.

Register a file before traversing its includes. Revisiting an already registered canonical path reuses its identifier without retraversal; this makes traversal of permitted include cycles finite. Hard links with different canonical paths remain distinct sources under the common rule. Implement traversal with an explicit stack so deeply nested includes cannot overflow the C stack.

Example: src/main.goat contains include "vector.goat" followed by include "matrix.goat". Both src/vector.goat and src/matrix.goat include "common.goat". Identifiers are main = 0, vector = 1, common = 2, matrix = 3. The include in matrix reuses common identifier 2.

Module.json fragment (other metadata omitted; entry values are illustrative and are not arithmetically derived from file identifiers):

```json
{
  "formatVersion": 1,
  "name": "geometry",
  "version": "1.0.0",
  "main": 0,
  "files": [
    {"name": "src/main.goat", "entry": 0, "external": false},
    {"name": "src/vector.goat", "entry": 40, "external": false},
    {"name": "src/common.goat", "entry": 80, "external": false},
    {"name": "src/matrix.goat", "entry": 120, "external": false}
  ]
}
```

This governs compiler-assigned indices, not execution order. Initialization remains lazy and follows executed include operations. The shared bytecode generator determines entry values; file identifiers are not instruction addresses. Numbering does not depend on filesystem enumeration or parallel processing completion order. Readers use validated stored main and files without renumbering or reconstructing the graph from unavailable sources.

Acceptance: the example produces exactly four records in the shown order; repeated include of common creates no duplicate. An include cycle reuses an assigned identifier without looping; a deep chain does not overflow the C stack. Includes in unexecuted branches participate in numbering. Rebuilding identical sources preserves the table regardless of filesystem enumeration order.

## Logical names of sources inside the project

For a source whose canonical path after symbolic-link resolution lies inside the real .gmod directory, files[].name is its path relative to that directory. Test containment by path components: /work/project-other is not inside /work/project. Canonicalize both the descriptor and source; accessing them through a link does not change the name.

Example: descriptor /work/geometry/geometry.gmod and source /work/geometry/src/vector.goat produce logical name src/vector.goat. Relocating the project tree without changing its relative structure preserves these names. Do not store the build machine's absolute prefix in .gbin.

Logical path-component separators are always `/`, including Windows builds. This converts separators specifically; a literal backslash in a Linux filename must not become `/`. Do not add a leading `./`; do not change component case, Unicode or spaces beyond the previously agreed source-identity canonicalization.

A link inside the project pointing to a source outside the real .gmod directory denotes an external source; do not use the link's relative path as its logical name. External-source naming is specified separately.

Acceptance: accessing one source directly or through a link yields the same logical name; relocating the project preserves names; internal Windows paths use `/`. A directory sharing only a string prefix is not considered nested. Classify an outward link as external; preserve literal backslashes in Linux filename components.

## Separate external-source flag

Each files record contains a required boolean external field. False denotes an internal source with a logical path relative to the real .gmod; true denotes a source outside that directory after canonicalization and symbolic-link resolution. This describes source provenance at build time, not an imported external library: both kinds of files belong to the current module.

For an external source, name contains only the actual filename without directories. Do not insert service prefixes such as @external, numbers or synthetic directories into name. The flag is stored in external; the number is already the files record index, with no separate id field. The real absolute path remains only in compiler state and is not written into .gbin.

Example fragment with two distinct external sources named math.goat (instruction addresses are illustrative):

```json
{
  "main": 0,
  "files": [
    {"name": "src/main.goat", "entry": 0, "external": false},
    {"name": "math.goat", "entry": 80, "external": true},
    {"name": "math.goat", "entry": 120, "external": true}
  ]
}
```

These are files 1 and 2 with independent scopes and states despite identical names. Show external status and indices separately in diagnostics. An external source can also be the entry file: main selects its record normally.

Acceptance: required external accepts only true/false; reject omission, null or a string instead of boolean. Internal names retain relative directories; external names contain only the filename. Do not combine distinct external paths sharing a filename. An outward link receives external = true. Verify that written names contain neither service prefixes nor absolute paths.

## Logical-name base for direct .goat execution

When no .gmod is present and a standalone .goat source is executed, use the directory of the real entry source after canonicalization and symbolic-link resolution as the base for logical names and external classification. Neither the process working directory nor the directory containing a link to the entry file defines this base.

The entry file receives index 0 and external = false; its logical name is its filename relative to its own directory. Other sources whose canonical paths lie within this base receive external = false and relative names using `/`. Sources outside it receive external = true and only the filename without directories; distinguish them by files indices. Test containment by path components, not string prefixes.

For example, /home/ivan/run.goat links to /work/app/main.goat. The base is /work/app. The entry receives name = main.goat; /work/app/lib/math.goat receives name = lib/math.goat, external = false; /work/shared/math.goat receives name = math.goat, external = true. Relative includes still resolve against the real containing source file, not the shared logical-name base.

Acceptance: direct and linked entry paths produce identical names and external flags; changing cwd does not change classification. Test an internal include, external include and an internal link to an external file. Building through .gmod retains the real .gmod directory as its base.

## File and instruction index ranges in v1

In module format v1, main, include file references and files[].entry use the unsigned 32-bit range: 0 through 4294967295 inclusive. The range is identical on all platforms. In JSON these are exact integers without fractional or exponent notation, not strings; parse without an intermediate double conversion.

The type range does not replace existence checks: main and each include reference must be below the actual file count; entry must identify an existing valid instruction for the corresponding file. An index equal to the element count is invalid. Values outside uint32, negative values and parsing/conversion overflow are errors, never truncation, saturation or wraparound.

The compiler checks representability before writing; the loader checks before indexing tables or executing code. Check all size/offset calculations and conversions to size_t for overflow and available address space. Schema-valid numbers do not guarantee that a module of that size can be loaded; container and memory limits still apply. Binary representations of these indices use 32-bit values with fixed byte order under the portable-bytecode rules, not native size_t.

This applies to the listed indices of the new module format. It does not declare every numeric metadata field or existing native-binding field 32-bit; those widths are specified separately.

Acceptance: test 0, 4294967295 and out-of-range 4294967296 without precision loss; the type maximum may fail object-existence checks but must not truncate. Test negative values, fractional/exponent notation, an index immediately beyond an actual table, size-calculation overflow and consistent interpretation across supported platforms. Boundary tests need not allocate billions of elements.

## Function identity within a module

A function has its own numeric function_id within its module. This is a separate identifier, not a file index, instruction position or function name. The mapping from function_id to its source file (files index) and corresponding FUNC instruction in the shared bytecode is stored separately. Names are not identifiers: anonymous functions and identically named functions in different scopes must remain distinct.

Native specializations reference the function_id of their module. Implementation selection includes module identity; equal numbers in different modules or versions do not identify the same function. Multiple specializations of one function retain its function_id and are distinguished by their specialization descriptions. Creating another closure does not create a new code identity: instances may share a function_id while retaining separate captured values.

Before execution, validate reference consistency: the function and file must exist, the FUNC instruction must be valid, and the binding must agree with the metadata. Missing references, contradictory bindings and duplicate definitions of a function_id are format errors. Metadata validation does not load native libraries through the OS; actual library ABI validation remains at its designated stage.

Acceptance: anonymous and identically named functions remain distinct; closure instances retain their own captures; equal function_id values in different modules or versions never select another module's native implementation. Reject invalid file, function and FUNC references. This decision does not specify the width of function_id or its numbering algorithm.

## Coverage of function identifiers

Assign a separate function_id to every function in the module, including nested and anonymous functions, regardless of exports or availability of native specializations. Do not number only native-compilation candidates. Adding or removing a native specialization for unchanged source code must not change function_id values. Verify identical identifiers when compiling the same source with and without native specializations, including nested and anonymous functions. Treatment of functions removed by optimization is specified separately.

## JSON fields of the function table

In module.json, functions contains records with three required numeric integer fields: functionId is the function's own module-local identifier; file is its source file index in files; instruction is the corresponding FUNC instruction index in the shared bytecode. Identity is determined by functionId, not the record's position in functions. A function name is not needed for this binding. The record schema is closed: in addition to these required identity fields, parameters and analysis are defined below. Unknown fields, missing required fields, null and incorrect types are errors.

file and instruction follow the agreed uint32 index rules, including file and instruction existence checks; instruction is an instruction index, not a byte offset. The width of functionId is specified separately. Unique functionId values and consistency with bytecode must be validated.

The following module.json fragment illustrates these tables, not a complete archive or the final list of all metadata fields. Instruction positions are illustrative. Function 1 is a named function in main.goat, 2 is nested within it, 3 is anonymous in the same file, and 4 belongs to lib/math.goat. These descriptions explain the example and are not additional record fields.

```json
{
  "formatVersion": 1,
  "name": "example",
  "version": "1.0",
  "main": 0,
  "files": [
    {
      "name": "main.goat",
      "entry": 0,
      "external": false
    },
    {
      "name": "lib/math.goat",
      "entry": 100,
      "external": false
    }
  ],
  "functions": [
    {
      "functionId": 1,
      "file": 0,
      "instruction": 12
    },
    {
      "functionId": 2,
      "file": 0,
      "instruction": 28
    },
    {
      "functionId": 3,
      "file": 0,
      "instruction": 54
    },
    {
      "functionId": 4,
      "file": 1,
      "instruction": 115
    }
  ]
}
```

## FunctionId assignment order

Numbering starts at 1 and is shared across all of the module's own files. Visit files in files array order; within each file, number functions by the position of their definitions in the source text. An enclosing function receives its number before functions nested within it. Include named, nested and anonymous functions. Assign identifiers before optimizations and native-specialization selection; selecting specializations does not renumber functions. Assignment is reproducible for identical sources and files order. Source changes may change numbers; functionId is not a persistent identifier across different builds. Representation of functions removed by optimization is specified separately and must not produce references to nonexistent FUNC instructions.

Acceptance: cover multiple files, nested and anonymous functions, enclosing-before-nested ordering and identical identifiers with different sets of native specializations.

## Basis of the externals table

In module.json, externals is an object whose keys are exported declaration names in the entry file's external interface. Each value describes the declaration. Key order does not define execution order. The general prohibition on duplicate decoded JSON keys applies.

The required boolean mutable field describes whether the declaration's value can be replaced: false for a constant, true for a variable. It does not guarantee immutable object contents or function purity.

The optional integer functionId field is written only when the analyzer proves that the declaration invariably references a particular function belonging to this module. The reference must resolve in the same module's functions table. For mutable = true, an initial function value provides no such guarantee; omit functionId and reject input metadata combining these fields. Absence of functionId does not imply a non-function value: the static binding may be unknown. The identifier describes function code, does not serialize a closure instance or its captures, and does not bypass module initialization.

Example for `external square = func(x) { return x * x; };` and `external var counter = 0;`:

```json
{
  "externals": {
    "square": {
      "mutable": false,
      "functionId": 1
    },
    "counter": {
      "mutable": true
    }
  }
}
```

This is the agreed basis of the description; the type vocabulary and representation of values and analytical properties are specified separately. Acceptance: preserve both declaration names and mutable flags; check functionId existence and reject mutable = true combined with functionId. Replacing an exported variable must affect subsequent calls rather than be bypassed by a static binding to its initial function. Generate and read metadata without executing dependency code.

## Proven type of an exported value

The optional type field in an externals description denotes a proven value type on which consumers may rely. It is not the initial value's type or an observation from one execution. Omitting type means the type is unknown, not that the value is absent. Even a constant may have an unknown type when its value is determined during initialization and the analyzer cannot prove its type without executing the dependency.

For a mutable export, its initial type is not a guarantee for subsequent reads: for example, external var counter = 0 allows later replacement with a string, so the initializer alone does not justify type = integer. Analysis and optimizations must not freeze the initial type of such a variable. mutable describes declaration mutability, whereas type describes a separate proven property of the value. For functions, type = function alone does not imply a known functionId, purity or absence of captures.

```json
{
  "externals": {
    "square": {
      "mutable": false,
      "type": "function",
      "functionId": 1
    },
    "counter": {
      "mutable": true
    }
  }
}
```

Acceptance: a proven own function receives type = function; an exported variable initialized with an integer receives no permanent integer guarantee; an unknown constant type does not prevent correct dynamic execution. Omitting type is not interpreted as an absent value or a prohibition on calls. The full type vocabulary and representation are specified separately.

## Proven scalar constant value

The optional value field in externals stores a scalar constant value proven at compile time. Consumers may use it for analysis without running the dependency. Omitting value means its value is unknown. A mutable variable's initial value is insufficient: value is forbidden when mutable = true.

Do not use value to serialize objects, arrays, functions, closure instances or captured state. Exact representations of supported scalar values and numeric bounds are specified separately. A recorded value must agree with the proven type; contradictory metadata is an error.

```json
{
  "externals": {
    "answer": {
      "mutable": false,
      "type": "integer",
      "value": 42
    }
  }
}
```

Knowing a constant value does not bypass module initialization on import. Constant substitution must not remove initialization required by an import, change its order or hide its exceptions and side effects.

Acceptance: the analyzer can access a proven scalar value without executing the dependency; unknown values are not invented; reject mutable = true together with value; composite objects and functions are not serialized through value. Using a known constant preserves initialization of the imported module and its observable behavior.

## Exact representation of integer value

For a scalar constant with type = integer, write value as an ordinary JSON number, not a string. Parsing, range validation, writing and canonicalization must preserve the integer exactly, without an intermediate double conversion. For example, 9007199254740993 must remain exactly 9007199254740993 rather than round to 9007199254740992.

Reject numbers outside the Goat integer type's range. Rounding, truncation and overflow with wraparound are forbidden. Canonicalization and main-hash computation must not collapse distinct integers through precision loss.

```json
{
  "mutable": false,
  "type": "integer",
  "value": 9007199254740993
}
```

Acceptance: exact read/write round trips for the example and its neighbor 9007199254740992; their canonical representations must differ. Test both Goat integer boundaries and values one step beyond each without intermediate overflow. Exact numeric bounds must be recorded in the final v1 schema and fixtures.

## Placement of function analysis metadata

Information about a function's parameters, result and side effects belongs in its functions table record. In externals, an exported declaration with a proven own function references that record through functionId. Multiple exported names may reference one function; its analysis description is not duplicated under each name.

```json
{
  "externals": {
    "square": {
      "mutable": false,
      "type": "function",
      "functionId": 1
    },
    "sqr": {
      "mutable": false,
      "type": "function",
      "functionId": 1
    }
  }
}
```

In this example, square and sqr reference the single functions record with functionId = 1. That record retains its required functionId, file and instruction fields. Its schema will be extended with explicitly defined analysis fields; the general closed-schema rule and rejection of unknown fields remain in force. Exact names and structures of the added fields are agreed separately.

Shared information describes function code and does not replace an individual closure's state. Analysis must preserve differences in captures and dependence on arguments; sharing a functionId does not prove equal captured values. Absence of a proven functionId binding neither prohibits dynamic calls nor permits attributing an arbitrary function's properties to the export.

Acceptance: two exported names of one function resolve to one record with shared analysis metadata; descriptions cannot diverge between aliases. Reading this metadata does not execute the dependency. Mutable exports still must not be permanently bound to their initial function.

## Function parameters in metadata

A functions record contains a parameters array in declaration order. Each element is an object with a required string name field containing the parameter name for diagnostics. A function without parameters has parameters = []. A parameter's zero-based array index is used in analytical relationships with arguments, for example “the result is the same as argument 0.” The name does not replace the index in such relationships.

```json
{
  "functionId": 1,
  "file": 0,
  "instruction": 12,
  "parameters": [
    {
      "name": "x"
    }
  ]
}
```

In this agreed basis, each parameter record contains name; additional analytical fields require explicit schema definitions. Unknown fields, incorrect types, null instead of an array or record, and a missing required name are errors. Preserve parameters order; do not sort by name.

A parameter's type is not inferred solely from an operation: x * x by itself does not prove that x must be an integer. Type constraints of a particular native specialization are described separately and do not become constraints on every call to the function. The parameter table does not change existing language rules concerning argument count at a call.

Acceptance: a function with multiple parameters retains their order and names; a function without parameters has an empty array. Analytical references use corresponding indices with bounds checks. An integer native specialization does not prohibit other valid calls through the VM.

## Structure of the function analysis contract

Schema design for issue #244: store the analysis contract in functions[].analysis independently of native specializations. Grounded in src/analysis/lattice.h, function_summary.h, function_summary.c, function_return.c and function_purity.c at commit 31f1f89d4e86a71f5375d4602bf0223d99d55965. This specifies metadata and implementation requirements; serialization is not yet implemented.

### Container and scope

analysis is an optional closed object with required formatVersion (integer 1) and summaries (array). Missing analysis or empty summaries provides no analysis guarantees. Existing functionId, file, instruction and parameters remain unchanged. externals references an own function through functionId; exported aliases do not duplicate descriptions.

Each summary is a closed object with required when and optional result, effects and normalReturn. All present claims hold together whenever when holds. Result facts concern normal returns only; they do not promise termination, absence of exceptions or absence of effects.

when is a closed object with optional arguments: one value description per formal parameter, in order. Missing arguments imposes no argument restrictions; when = {} is unconditional and requires proof for all calls and admissible environments. arguments = [] is for a function without parameters. Use {} for an unconstrained parameter. Missing actual arguments are null under language rules. Extra actual arguments create no formal parameters, but their evaluation and effects must be preserved.

Conditions describe input values. Summaries do not fix capture values: current specialization keys do not do so. Never promote a single concrete call observation to a universal contract. If no summary applies, results and effects are unknown; language-valid calls remain permitted.

Analysis summaries do not select an executable implementation. Overlapping conditions are allowed: simultaneously applicable proven facts must agree. Array order gives no priority. Consumers may use one applicable summary or safely intersect several, never choose a convenient claim from contradictory facts. Detectable contradictions are metadata errors. This differs from the ban on ambiguous native-specialization selection.

### Shared value description

when.arguments[] and result use the same closed ValueFacts format. Previously agreed type/value in externals keep their location and share these semantics. ValueFacts fields:

- type: optional string from null, boolean, integer, real, numeric, character, string, function, array, object. numeric means integer or real; object means a user-defined object, not any value.
- nonNull: optional true, proving exclusion of null. Exact null uses type = null. JSON null is not a facts object.
- value: optional exact scalar constant consistent with type. Omission does not mean null. An explicit scalar type is required. Integers are exact JSON numbers in int64 range, never passing through double. Character encoding is defined in “Character constant representation.” Real encoding, including special values, is defined in “Bit encoding of real constants.”
- range: optional closed object with required integer min/max, for type = integer. Inclusive int64 bounds, min <= max; any value must lie inside the range.
- elements: optional allowed-element description for type = array. It uses ValueFacts structurally to allow future growth; the initial producer is limited by LATTICE_TYPED_ARRAY to an unrefined element type, nonNull, boolean constants or an empty description. Do not invent nested shapes, ranges or element values.
- empty: optional true for type = array, proving an empty array. Represents today's typed array with BOTTOM elements without confusing it with an impossible function result.

{} provides no refinements. Facts must agree; null combined with nonNull = true is invalid. Separate fields are not logically independent: value refines type and range. A type does not imply deep immutability, object identity or absence of shared nested references.

normalReturn, when present, is false: no normal return is possible under the conditions. It does not distinguish exceptions, exit or divergence and does not guarantee termination. result must be absent when normalReturn = false. Missing normalReturn does not promise return. Emit only a completed BOTTOM proof, never an intermediate approximation or exhausted analysis budget.

### Effects

effects is a closed object with required may (array of unique strings) and unknown (boolean). Initial may categories: input, output, externalRead, externalWrite. They conservatively bound possible effects under the existing analyzer's definitions; they do not assert each action occurs on every call.

unknown = false means the description is complete relative to this effect system; categories absent from may are excluded. unknown = true means incomplete knowledge; missing categories are not proved impossible. Missing effects means no knowledge, not an empty may with unknown = false.

Empty may with unknown = false expresses current purity. It does NOT prove no exceptions, termination, no allocations, deep object independence or permission to delete a call. externalRead concerns state external to the function, not only other modules. Immutable-binding reads excluded by existing aggregate-effect analysis must not be misreported as mutable reads.

Export transitive effects including called functions. direct_effects, traversal status, iteration counts, C-backend blockers and AST addresses are not program guarantees. Direct captures are already collected, but detailed references need portable declaration identity; neither a name alone nor a memory address suffices. Until that schema is defined separately, use aggregate externalRead/externalWrite rather than pretending a detailed capture wire format is finished.

### Example

Fragment for identity = func(value) { return value; }; instruction positions are illustrative. Both analysis signatures must actually have been registered and proved. This does not restrict calls to two types; other calls simply lack information.

```json
{
  "externals": {
    "identity": {
      "mutable": false,
      "type": "function",
      "functionId": 1
    }
  },
  "functions": [
    {
      "functionId": 1,
      "file": 0,
      "instruction": 12,
      "parameters": [
        {
          "name": "value"
        }
      ],
      "analysis": {
        "formatVersion": 1,
        "summaries": [
          {
            "when": {
              "arguments": [
                {
                  "type": "integer"
                }
              ]
            },
            "result": {
              "type": "integer"
            },
            "effects": {
              "may": [],
              "unknown": false
            }
          },
          {
            "when": {
              "arguments": [
                {
                  "type": "real"
                }
              ]
            },
            "result": {
              "type": "real"
            },
            "effects": {
              "may": [],
              "unknown": false
            }
          }
        ]
      }
    }
  ]
}
```

The existing summary proves a return type for a parameter type, but does not store the general relation “returns the same argument.” Consequently identity/typeOfArgument/sharing are not added to the example.

### Producer, evolution and acceptance

Current function_summary_type removes constants, ranges, callable identity and array refinements from return_type. The initial producer exports retained general types, NOT_NULL, completed BOTTOM proofs and aggregate effects. A richer ValueFacts schema can represent facts; it does not authorize inventing proofs. Preserving refinements across all normal exits is separate analyzer work. Facts for a concrete call, captures or individual expressions cannot replace a universal summary.

Improved analysis using existing fields does not change the version. New fields or relation kinds require a new analysis.formatVersion with explicitly implemented reader support. Reject unknown versions, fields and enum values rather than silently ignoring them. Do not reserve fictional identity, sharing, callback or exception fields now. Future versions extend the same result/when/effects structure without conflating value descriptions and function behavior.

Before closing #244, add the agreed scalar encodings, empty-result normalization, may/summaries ordering and duplicate checks to fixtures; finalize other explicitly open normalization rules. Applicable facts do not depend on array order; serialization order and hashing must nevertheless be deterministic.

Acceptance: closed-object and version validation; formal-parameter count; unknown versus null; non-null; int64 and interval boundaries; empty arrays; fact consistency; missing and incomplete effects; no false purity; overlapping consistent summaries and detectable contradictions; no invented facts for unanalyzed signatures; no transfer of facts between distinct closure environments. Metadata neither executes dependencies nor bypasses import initialization nor changes native-implementation selection rules.

## Bit encoding of real constants

For type = real, value is a string of exactly 16 lowercase hexadecimal digits [0-9a-f], encoding IEEE 754 binary64 bits from most to least significant. Reject 0x prefixes, whitespace, uppercase and other lengths. This encodes a real value, not a Goat string value. Host byte order does not affect the representation. The rule applies equally to externals and ValueFacts descriptions.

Examples: 1.5 is "3ff8000000000000"; +0.0 is "0000000000000000"; -0.0 is "8000000000000000"; positive infinity is "7ff0000000000000"; negative infinity is "fff0000000000000".

```json
{
  "type": "real",
  "value": "3ff8000000000000"
}
```

Writing, reading and canonicalization preserve exact bits: no decimal conversion, rounding, zero-sign changes or NaN normalization. First parse a 64-bit bit pattern, not a number to convert arithmetically to double. The representation permits every binary64 pattern, including subnormals, infinities and NaNs.

The producer emits an exact NaN only with proof of its bit pattern. The current lattice's grouping of distinct NaNs is not such proof. If only a real value or an unspecified NaN is known, omit value and retain type = real. Readers must not perform arithmetic on NaNs to validate metadata or change their payload/sign during canonicalization.

Acceptance: bit-exact read/write round trips for finite and subnormal values, both zeros and infinities; preserve distinct NaN patterns without asserting that today's analyzer can prove each one. Validate length, alphabet, lowercase, and rejection of 0x, whitespace and JSON numbers in place of strings. Canonical representations of +0/-0 and distinct NaN patterns differ.

## Character constant representation

For type = character, value is a JSON string containing exactly one decoded Unicode scalar value: a code point U+0000–U+10FFFF excluding U+D800–U+DFFF. This applies to externals and ValueFacts. It represents a Goat character; string values use type = string.

```json
{
  "type": "character",
  "value": "Я"
}
```

A non-BMP character is permitted even when it occupies two UTF-16 code units. A valid pair of JSON surrogate escapes decodes to one scalar value; lone or incorrectly paired surrogates are errors. Count decoded scalar values, not UTF-8 bytes, UTF-16 code units or visual graphemes. Reject empty strings and multiple scalar values.

The null character is allowed and is written using the JSON escape \u0000. A space is a valid character. Do not trim, case-fold or normalize Unicode: "é" contains one scalar value, whereas "e\u0301" contains two and is invalid for character. Equivalent JSON escape spellings of the same character denote the same value and follow general JSON canonicalization without changing Unicode content.

Acceptance: BMP and non-BMP characters, the latter in UTF-8 and escaped surrogate-pair form; U+0000; space; empty strings; two characters; lone/reversed surrogates; e + U+0301. Verify value-preserving round trips on platforms with different wchar_t widths. Code points outside the Unicode scalar range must not be serialized as character constants in this format.

## String, boolean and null constants

For type = string, value is an ordinary JSON string. Empty strings, leading/trailing spaces, whitespace-only strings and embedded null characters are real values and must be preserved exactly after JSON decoding. Do not trim, case-fold or normalize Unicode. JSON escapes follow the general rules; encode a null character as \u0000.

For type = boolean, value is JSON true or false, never a string or number. false is a proven value, not missing information. Omitting value for string or boolean means an unknown value of the respective type.

Exact null needs only type = null: this type has a single value. Canonical writers omit a redundant value field. This differs from {}, which gives no type or value information, and from JSON null in place of a description object, which is invalid.

```json
{ "type": "string", "value": "" }
```

```json
{ "type": "boolean", "value": false }
```

```json
{ "type": "null" }
```

These rules apply to ValueFacts and the agreed externals fields. Empty informational-field normalization (description, license, etc.) does not apply to program constants. Optimizers and canonicalizers must not discard a known empty string or false. Known values still do not bypass import initialization.

Acceptance: distinguish unknown strings from known empty strings, unknown booleans from known false, and unknown {} from exact null. Preserve leading/trailing spaces, whitespace-only strings, \u0000 and Unicode sequences without normalization. Reject incorrect JSON value types for string/boolean. Canonical null descriptions contain no value field.

## Normalizing an unknown result and positional arguments

An empty result = {} is equivalent to an omitted result: neither provides result facts. Readers accept both in contexts where result is allowed. Canonical writers omit an empty result; normalize before canonicalization and main-hash computation. A difference consisting only of this empty object does not change analytical guarantees or the main hash.

This rule does not remove empty elements from the positional when.arguments array. For example:

```json
{
  "when": {
    "arguments": [
      {},
      { "type": "integer" }
    ]
  }
}
```

The first element places no refinement on the first formal argument, while the second constrains the second argument to integer. Every element, including empty leading, middle and trailing elements, retains its index. Removing, compacting or sorting elements is forbidden. The array length must still match the formal parameter count.

This rule concerns empty result, not arbitrary format objects or arrays. It does not discard known value = "" or value = false and does not turn type = null into an unknown value. The prohibition on result together with normalReturn = false still applies to the input structure before normalization; an empty result cannot bypass it.

Acceptance: missing result and result = {} normalize identically; known properties remain. Empty leading, middle and trailing argument descriptions survive reading, writing and canonicalization without changing count or indices. Distinguish arguments = [{}, {"type":"integer"}] from arguments = [{"type":"integer"}, {}].

## Analysis summary ordering and duplicates

analysis.summaries order has no semantic significance. Readers accept any order. Writers and main-hash computation use the same representation: validate the input schema, apply format-defined normalization to each summary, obtain its canonical JSON in UTF-8, then sort summaries lexicographically by those bytes. Compare unsigned bytes; a shorter sequence sorts first when it is a prefix of another. Do not use locale, type names or analyzer discovery order.

Identical summaries after normalization and canonicalization are duplicate errors; do not silently remove them. In particular, summaries differing only by omitted result versus result = {} are duplicates. Readers, not only writers, must check duplicates.

Distinct summaries with overlapping conditions are allowed when their claims agree. Equal when objects alone do not make summaries duplicates: compare the entire normalized summary. Existing detectable-contradiction checks remain. Summary order neither resolves contradictions nor selects a native implementation.

Acceptance: permutations of summaries yield identical canonical data and main hashes; key ordering and equivalent JSON escapes cannot conceal duplicates; neither can omitted versus empty result. Preserve distinct consistent summaries with equal or overlapping conditions. Never reorder positional when.arguments or parameters.

## Canonical ordering of effects.may categories

effects.may is an unordered set of effect categories. Readers accept valid names in any order. Writers and main-hash computation sort decoded names lexicographically by unsigned UTF-8 bytes, independently of locale. The full order for current categories is externalRead, externalWrite, input, output.

```json
{
  "effects": {
    "may": ["externalRead", "externalWrite", "input", "output"],
    "unknown": false
  }
}
```

A repeated category after JSON decoding is an input metadata error, not permission to silently deduplicate. Unknown names remain forbidden by the schema version. Normalize may order inside each summary before canonicalizing it, comparing summaries or sorting analysis.summaries. Consequently category permutations neither change the main hash nor conceal duplicate analysis summaries.

Sorting does not change unknown or remove effects. Empty may with unknown = false retains its meaning of proven purity under the current effect system.

Acceptance: permutations of the same categories yield identical canonical data and hashes; reject duplicates including equivalent JSON escape spellings. Detect summaries differing only in may order as duplicates after normalization. Cover empty may and the distinction between unknown = true and false.
