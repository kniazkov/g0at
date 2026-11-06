# Diagnostics, inspection and generated data

[Contents](index.md) · [Русский](../ru/tools.md)

## Keep diagnostics at the right layer

`src/resources/messages.*` holds localized compiler/runtime messages, while
`src/common/compilation_error.*` represents positioned diagnostics and severity.
The launcher renders them before releasing their storage. Analysis observations
from `collector.*` are structured proof events, not user errors; tests can select
an event without interpreting a localized message.

Graph visualization uses node display data and relations. It is a view, not an
execution engine, and a display classification must not alter semantics.
Graph scenarios inspect SVG structure with a test-only limited reader rather
than accepting a successful Graphviz process as proof of the graph content.

## Inspector and merger

`src/inspect.c` and `src/cli/inspect*` coordinate validation reports. A failed
prerequisite makes dependent checks skipped, not passed. Optional probe status
includes the load/descriptor/symbol/unload/exit stages so a crash or timeout is
not confused with invalid metadata. inspect_probe_module() requires a retained,
validated graph and runs native code only in its child. Process isolation here
is not a security sandbox for hostile native libraries.

`src/merge.c` coordinates merge APIs. Information-field conflict resolution and
native variant merging have separate contracts in `src/module`. Tests construct
libraries with observable constructors and verify that inspecting/merging without
an explicit load trial does not execute them. JSON reports are parsed structurally;
substring matching cannot validate keys, types, duplicates or skipped-check reasons.

## Generated data has a source of truth

| Generator | Input | Output | Normal check |
| --- | --- | --- | --- |
| `tools/unicode` | Vendored Unicode UCD files | `lib/strings/unicode_data.h`, input hash manifest | `unicode_generator --check` |
| `tools/module-schema` | `docs/module-format-v1/schema-source.json` | Public JSON schemas, `src/module/module_json_schema.h` | `module_schema_generator --check` |

Both are native C tools with standalone CMake builds, so host generation does not
depend on running a target interpreter during cross-compilation. Ordinary builds
consume committed outputs; explicit updates belong in reviewed changes. Read the
[Unicode instructions](https://github.com/kniazkov/g0at/blob/master/tools/unicode/README.md)
and [schema instructions](https://github.com/kniazkov/g0at/blob/master/tools/module-schema/README.md)
before regenerating. Never fix a generated table without updating its source.

## Where to investigate a failed tool check

Generator tests include stale/missing output and malformed input, not only successful
round trips. Inspector and merger scenarios keep commands, captures and workspaces.
Format vectors exercise byte-level contracts independently of the host platform.
Documentation generation is another tool check: a valid Doxygen build verifies
references and syntax, not the truth of every explanatory sentence.
