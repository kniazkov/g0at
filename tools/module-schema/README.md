# Native module schema generator

`module_schema_generator` replaces `scripts/generate_module_json_schema.py`
(step #332 of the [migration checklist](../../docs/c-tooling-migration-checklist.md)).
Its only input is `docs/module-format-v1/schema-source.json`. It emits the four
public `*.schema.json` files beside that source and
`src/module/module_json_schema.h`. `schema.py` remains a transitional reader for
`reference.py`; it is not used by this generator.

## Build and run

From the repository root:

```sh
cmake -S . -B build/native
cmake --build build/native --target module_schema_generator
build/native/module_schema_generator --check
```

The generator can also be built alone, with no interpreter, common library,
Python, generated header, or Unicode-table dependency:

```sh
cmake -S tools/module-schema -B build/schema-host
cmake --build build/schema-host --config Release
ctest --test-dir build/schema-host -C Release --output-on-failure
```

On multi-configuration generators, the executable is in the `Release/`
subdirectory; on Windows it has the `.exe` suffix. For cross-compilation,
build this standalone directory separately with a **host** compiler and run
that host executable. A normal target build never executes a generator.

`--root PATH` selects a repository/output tree (default: the source checkout
recorded at configure time). `--source PATH` selects another input JSON file.
Neither option depends on the working directory. `--check` compares all five
outputs, fails on missing or stale files, and never writes them. Omit `--check`
only to explicitly regenerate reviewed changes. Output is UTF-8/LF on all
platforms; `--check` also accepts CRLF checkouts without rewriting them, matching
the old Python text-read behavior. Unknown keywords, types, references or patterns are errors, not
silently ignored extensions. Duplicate decoded keys and non-integer numeric
tokens are rejected before writing any output.

The historical generated-header banner is retained to make this migration
byte-for-byte reproducible; it is provenance, not a command to invoke Python.
Ordinary builds continue to consume the committed artifacts.

## Verification and migration coverage

The pre-migration Python generator and `schema.py` public schema renderer were
compared against the native results on the same JSON input. All five committed
artifacts must remain byte-identical. The Python generator is removed only after
that comparison; existing independent format-reference tests remain unchanged.

| Old check / contract | Native CTest coverage |
| --- | --- |
| Python generator `--check` for the C header | `format.schema` (also checks all four public schemas) |
| Public schema rendering against reviewed artifacts | `format.schema.roundtrip` |
| Read-only check rejects stale/missing output | `format.schema.stale`, `format.schema.missing` |
| Unsupported schema input must not be silently accepted | `format.schema.reject-keyword`, `format.schema.reject-pattern`, `format.schema.reject-type`, `format.schema.reject-ref`, `format.schema.reject-float`, `format.schema.reject-duplicate` |
| Production table constraints must not silently lose semantics | `format.schema.reject-array-items`, `format.schema.reject-ref-siblings`, `format.schema.reject-zero-items`, `format.schema.reject-zero-length`, `format.schema.reject-unique-nonstrings`, `format.schema.reject-ref-cycle` |

Run the regression scenarios with:

```sh
cmake --build build/native --target module_schema_generator_testing
ctest --test-dir build/native --output-on-failure -L module-schema
```

Each regression run uses the shared scenario framework for a unique build-tree
workspace, deadline, child-process diagnostics and cleanup. Failures preserve
the workspace; `--keep-work` retains successful runs too. The Linux and Windows
build matrix runs these scenarios, while the format-vector matrix additionally
builds and checks the standalone host generator.
