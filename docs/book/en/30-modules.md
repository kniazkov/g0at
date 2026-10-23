# Module System

[Contents](index.md) · [Русский](../ru/30-modules.md)

> Status: the module system is being implemented incrementally. This stage introduces
> identifier validation and strict JSON descriptor parsing. Module compilation,
> `include`, `import`, and execution of `.gbin` are planned for later tasks.
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
`string_view_t` contract remains unchanged. A separate wide-character
parser supports the compiler's `wchar_t` strings.

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

This step uses transitional signatures `GOATST01` (container) and `GOATSC01`
(inner image), version 1. They reject all old images, including `GOATBI11` and
`goat v.1`, with a request to recompile. They deliberately do **not** claim the
final `GOATBC01` layout: the remaining sections and container migration belong
to #251. The independent `test/unicode/scalars.gbin.hex` fixture is read unchanged
by the C loader in the Windows/Linux CI matrix. `scripts/check_unicode.py`
checks VM, folding, native execution, embedded zeros and locale independence.
