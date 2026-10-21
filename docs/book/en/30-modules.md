# Module System

[Contents](index.md) · [Русский](../ru/30-modules.md)

> Status: the module system is being implemented incrementally. This stage introduces
> identifier validation and strict JSON descriptor parsing. Module compilation,
> `include`, `import`, and execution of `.gbin` are planned for later tasks.
> See the complete [module design contract](modules.md).

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
`main` is the entry `.goat` file path, relative to the `.gmod` file.

The optional `dependencies` object maps module names to exact string versions.
The optional `native` object specifies `off`, `auto` (the default), or
`required`, and a `target` platform (default `host`). Compiler paths
and flags belong to command-line options or the environment, not `.gmod`.

Unknown fields and duplicate JSON keys are rejected, including keys that become
identical after decoding `\\u` escapes. Input must be valid UTF-8 without BOM.
Unicode strings are not normalized. Integer fields are parsed exactly, without
rounding through `double`.

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
