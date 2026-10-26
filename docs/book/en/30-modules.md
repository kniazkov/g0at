# Module System

[Contents](index.md) · [Русский](../ru/30-modules.md)

> Status: the module system is being implemented incrementally. Validation, module
> snapshots, include/import execution and serialized initialization are implemented
> through shared compiler/runtime APIs. Full `.gmod` CLI integration remains #191.
> See the complete [module design contract](modules.md).

Exact formats and reference vectors: [v1 specification](../../module-format-v1/README.md).

## Build descriptor

To build a module, create a JSON file with the `.gmod` extension.
For example, `geometry.gmod`:

```json
{
  "formatVersion": 1,
  "name": "geometry",
  "version": "1.2.0",
  "main": "src/main.goat",
  "dependencies": {
    "math": "2.0.0"
  },
  "native": {
    "mode": "auto",
    "target": "host"
  }
}
```

`formatVersion`, `name`, `version`, and `main` are required.
`formatVersion` must be the JSON integer `1`, not `1.0` or `1e0`.
`name` identifies the module; `version` is its exact string version.
`main` is a literal relative or absolute path to the entry source.
Relative paths resolve from the real `.gmod` directory after resolving symbolic
links, independently of the working directory. Sources may be outside the project;
the `.goat` extension is recommended, not required. Environment variables and `~`
are not expanded. Empty paths and `NUL` are forbidden; whitespace is not trimmed.
After resolving links, the source must be a regular file. `/` works on every target
OS; backslashes follow OS rules. On Windows, incomplete paths such as `C:main.goat`
and `/src/main.goat` are forbidden; the full `C:/src/main.goat` is allowed.

The optional `dependencies` object maps module names to exact string versions.
The optional `native` object specifies `off`, `auto` (the default), or
`required`, and a `target` platform (default `host`). Compiler paths
and flags belong to command-line options or the environment, not `.gmod`.

Unknown fields and duplicate JSON keys are rejected, including keys that become
identical after decoding `\\u` escapes. Input must be valid UTF-8 without BOM.
Unicode strings are not normalized. Integer fields are parsed exactly, without
rounding through `double`.

## Descriptor fields v1

All objects are closed: only the listed fields are allowed. `null` is forbidden.
Validate the entire `.gmod` before applying command-line overrides, including
native build settings when `mode = off`.

| Field | Type | Required / when absent |
| --- | --- | --- |
| `formatVersion` | Integer `1` | Required |
| `name` | Module-name string | Required; rules below |
| `version` | Exact-version string | Required; rules below |
| `main` | Path string | Required; rules above |
| `dependencies` | Name-to-version object | Empty object |
| `native` | Object with `mode` and `target` | `auto` and `host` |
| `native.mode` | `off`, `auto`, `required` | `auto` |
| `native.target` | Target-platform string | `host` |
| `description`, `license`, `homepage` | Strings | Absent |
| `authors` | Array of author objects | Empty list |
| `authors[].name` | Nonempty string | Required in each entry |
| `authors[].email` | String | Absent |

`native.mode` and `native.target` are independently optional;
`native: {}` and `dependencies: {}` are valid. The exact set of explicit native
targets still needs to be fixed in issue #244; this table does not substitute
an arbitrary string for that set. `host` resolves to a concrete platform at build time.

Empty `description`, `license`, `homepage`, `email`, and `authors: []` are
equivalent to absence; whitespace is not empty. Informational strings retain
whitespace, case, and Unicode sequences unchanged. `email` and `homepage`
undergo no address/URL syntax validation or network requests.
Authors retain their order; duplicate `name + email` pairs after empty-email
normalization are forbidden. Author objects contain only `name` and `email`.

Versions in `dependencies` are defaults for imports without `@`.
Explicit imports may use multiple versions of the same module;
the compiled `module.json` represents dependencies as an array of
`name`, `version`, `hash` records, not the `.gmod` object.

## Names and versions

Module names, version strings, and keys and values in `dependencies` use
the same character restrictions:

- Only lowercase ASCII letters `a-z`, digits `0-9`, period `.`,
  underscore `_`, and hyphen `-`. No whitespace, Unicode, or embedded `NUL`.
- The first character must be a letter or digit. Empty strings are forbidden.
- A trailing period is forbidden; repeated interior periods and trailing
  hyphens and underscores are allowed.
- Windows device names are forbidden on every platform: `con`, `prn`,
  `aux`, `nul`, `com0`–`com9`, `lpt0`–`lpt9`, including names
  with extensions such as `con.txt`.
- Maximum length: **255 bytes** for names and **250 bytes** for versions;
  permitted ASCII characters each occupy one byte.
- Case is never corrected automatically. Versions are opaque strings,
  not SemVer, with no numeric parsing or ordering. `1.02.0` and `1.2.0`
  are distinct.

These restrictions support consistent module paths on Linux and Windows:
`geometry/1.2.0.gbin`. Validation is locale-independent.

## Importing dependencies

The agreed source syntax is:

```goat
const math = import "math";          // Version from dependencies.
const oldMath = import "math@1.2.0"; // Explicit version.
```

Without `@`, the version must be provided by `.gmod`. With `@`,
both the name and version are mandatory. Multiple `@` characters are invalid.
An explicit version affects only that import.

The shared C API for name validation and import-specifier parsing is declared
in `include/goat/module_identity.h`. It accepts explicit lengths, does not
require null termination, and does not allocate memory. The existing
`string_view_t` contract remains unchanged. `goat_module_name_valid` and
`goat_module_version_valid` accept bytes; their `_wide` counterparts accept
portable `gchar_t` (UTF-32) elements. Both storage types share one implementation
of the grammar and reserved stems: `con`, `prn`, `aux`, `nul`, `com0`–`com9`,
`lpt0`–`lpt9`, before the first dot. Thus `con..txt` is invalid, while `con-`,
`com10` and `x.con` are valid. Public limits are `GOAT_MODULE_NAME_MAX_LENGTH`
(255) and `GOAT_MODULE_VERSION_MAX_LENGTH` (250). Since identifiers are ASCII,
valid byte and UTF-32 element counts coincide.

`goat_module_parse_import` and `goat_module_parse_import_wide` borrow slices of
the original buffer without copying. Without `@`, `has_version` is false and
the version slice is null/empty; version resolution happens later. Failure
clears every result field; a null result pointer returns false. Result storage
must not overlap the input. Oversized imports are rejected before reading the
buffer: the maximum is `255 + 1 + 250 = 506` elements. Embedded NUL, whitespace,
uppercase and non-ASCII characters are rejected, never repaired. `1.02` and
`1.2` are distinct exact versions; `latest` is an ordinary literal version with
no special meaning. The JSON reader uses these same validators for identities,
dependencies and `functionRef` records, including unused dependencies.

## Portable scalar strings (#250)

All internal strings, compiler messages, slices and native runtime strings now
use `gchar_t`, an alias of C11 `char32_t`. A scalar occupies exactly four
8-bit bytes. Literals use `U"..."` and `U'...'`. Configuration and public-header
checks reject big-endian and unknown byte order, non-UTF-32 targets and other
widths. These checks concern the compilation target, including cross builds.

Lengths and indices count Unicode scalars, not UTF-8 bytes, UTF-16 units or
visual graphemes. A supplementary character has length one on Windows as on
Linux; `e` followed by U+0301 has length two. No normalization occurs. U+0000 is
ordinary content: `"A\0B"` has length three and differs from `"A"`. Terminated
views retain the extra zero outside their length; bounded slices need no extra
readable element. Ownership and the null/empty distinction are unchanged.

The string library supplies bounded copying, comparison, searching, builders,
formatting (`%v` takes a whole view), escaping, hashing and strict UTF-8/UTF-16
boundaries. Invalid Unicode is rejected, never repaired. OS path conversion
rejects zeros anywhere in the input before calling a terminated system API.
Unicode casing is pinned to **17.0.0**: strings use full default mappings
(`ß → SS`, contextual Greek final sigma), single characters use simple mappings.
Compiler folding, VM and native runtime share tables and algorithms. UCD inputs,
license and SHA-256 manifest are in `third_party/unicode`; regenerate with
`python scripts/generate_unicode.py`. Builds never download Unicode data.

String payloads are explicitly serialized as UTF-32LE without BOM, with one
four-byte terminator outside the declared content length. The loader checks
length, bounds, alignment, terminator and every scalar, then uses the loaded
buffer directly. No per-string allocation, transcoding or byte swapping occurs.

## Portable bytecode v1

The compiler now writes the fixed `GOATBC01` image described in
[the wire specification](../../module-format-v1/README.md#direct-use-bytecode-image).
Its 64-byte header addresses instructions, typed data descriptors, file records,
function records, source-location runs and payload. All fields have fixed widths
and explicit little-endian encoding. No C structure padding, pointers or `size_t`
are persisted. Layout assertions permit direct const views after validation.
The current compiler produces one file span; source-order function IDs retain
removed-function gaps. Source positions and function ownership are recorded per
instruction and compressed into runs. Generated positions use line/column zero.
No source text or absolute path is stored in this image.

A standalone `.gbin` wraps exactly those bytes in `GOATPG01` v1 with a checksum,
optional native library SHA-256 and external binding records. It does not use a
second bytecode codec. The loader also accepts a bare image for tools and module
integration. ZIP module packaging and JSON/table cross-validation remain separate
tasks. Module opcodes have reserved v1 IDs; their execution requires the future
module loader. Native machine code remains specific to its target.

The loader rejects previous `GOATST01`, `GOATSC01`, `GOATBI11`, `goat v.1` and
other old images with a request to recompile. There is no migration reader.
Bytecode, standalone envelope and native ABI use independent version counters;
each is currently 1. Incompatible changes must increment the affected version.

Instructions, tables and strings remain immutable through validation, VM execution
and native attachment. Runtime identities, caches and native addresses live outside
the buffer. Strings point directly into its UTF-32LE payload. The owner retains the
image until all users have finished. The common codec validates bounds, overflow,
padding, scalar values, data kinds, function/entry references and table coverage.
Module dependency/JSON semantics belong to #184 and the subsequent loader tasks.

`test/binary-v1/scalars.hex` and `test/unicode/scalars.gbin.hex` are independent
Python-produced fixtures, consumed unchanged by the actual C loader on every
Linux/Windows CI target. `scripts/check_bytecode_v1.py` checks numeric bits,
function metadata, malformed records and raw/wrapped equivalence. Unit/native
pipeline tests compare image bytes before and after validation, execution,
native preparation, fallback and worker-thread execution.


## File and module access boundaries (#204)

The runtime identifies a source file by its immutable file-table index **and the
retained module instance**, not its path, display name or a globally shared index.
Two instances both containing file 0 therefore have independent permissions.
The internal multi-file API creates one persistent scope per file; `include` and
`import` execution and loader caching remain subsequent stages.

| Declaration | Who may access the cell |
| --- | --- |
| No modifier | Code defined in the same file of the same module instance |
| `public` | Code defined in any file of the same module instance |
| `external` | Code from any module |
| `protected`, `private` | Existing object-domain and inheritance rules |

`external var value = 1;` and `external function = func() {};` use the same
single-modifier syntax as `public`; omitting `var`/`const` implies a constant.
The wire values remain 0, 1, 2, 3, 4 respectively; bytecode version is unchanged.

Methods, callbacks and returned closures retain the file of their definition,
regardless of who calls them or which receiver is used. The same identity controls
reads, writes, keys and recursive rendering, including nested arrays. The analyzer
only proves file/module access when source-root identity establishes it; unknown
provenance remains checked by the VM rather than inferred from equal names.

Another file in the same module may add a field, which belongs to the adding file.
An inaccessible winning property is an access error, not permission to create a
second cell. Another module cannot extend the original object, but may write its
accessible `external var` cells. `clone` and `flat` produce objects owned by the
caller's module/file while retaining every copied cell's original file, module and
modifier. Transferred methods retain their defining-file rights and relocate their
captures; copying does not make hidden fields visible.

`scripts/test_module_access.py` checks these rules through internal multi-file
fixtures and real VM execution, including independent module instances, GC and
unchanged wire bytes. End-to-end `include`/`import` integration is tracked by #218.


## Serialized initialization and equal execution threads (#217)

Included/imported initializers share one process-wide recursive initialization
mutex. The initializing thread may reenter a cyclic include and obtain the same
partial scope; another thread waits and observes the completed scope or the exact
saved exception and original trace. Independent initializers cannot overlap.
Normal calls into ready files do not acquire this initialization mutex.

A separate recursive execution mutex protects shared runtime state, including
publication of declaration readiness, values, file states and retained failures.
It is released at VM instruction boundaries and around blocking thread waits and
supported blocking I/O. Releasing this mutex does **not** release an initializer's
mutex or clear its dynamic restrictions. Internal loader waits are not user joins;
initialization has no language timeout. Subsequent user read-modify-write sequences
are not promised to be atomic.

`spawn(callable, ...arguments)` starts an OS-backed execution using the existing
VM `thread_t` model and returns a positive, process-local integer thread ID.
`join(id)` waits for that execution and returns its result or propagates its
original exception. Both operations raise `Exceptions.INVALID_OPERATION` while
the caller's dynamic included/imported initialization depth is nonzero, including
calls through functions from otherwise ready files. Nested initialization restores
the previous depth on success and failure. Already ready includes/imports do not
start a new initialization context.

The directly launched entry file uses `initialize_module_file_direct`, not the
ordinary included/imported gate. Its top-level execution holds no initialization
mutex: already published functions and variables are available to other threads,
while access to a pending declaration throws without waiting for the entire file.
Its ordinary functions can therefore spawn a thread and join it before the entry
file finishes. File/module state, snapshots, caches and native support remain alive
until **all** program threads finish, including threads created by other surviving
threads. Completion of the entry execution does not terminate the VM.

See [the implementation API](../../module-serialization-api.md). These rules do not
introduce user mutexes or complete the separate bounded `exit`/cleanup protocol
tracked by #230.
