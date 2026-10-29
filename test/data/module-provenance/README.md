# Function and field provenance fixtures (#360)

Copyright 2026 Ivan Kniazkov. Distributed under the repository MIT license.

`scenarios.json` preserves the exact source texts, file maps, compilation order,
failure flags and expected stdout from `scripts/check_module_function_provenance.py`
at commit `d40c232fe471af61fdf39424c2d2b192319f6dbb`. The table was captured while
executing the original Python runner, not by deriving expectations from the VM.
It has **13 independent projects and 36 compile/VM checks** covering rules 1–12.
There are no random inputs or platform-specific branches.

The C runner is
[`module_function_provenance_scenarios.c`](../../src/functional/module_function_provenance_scenarios.c).
It consumes the table read-only and reuses `test/support/module_execution` for
all compilation/execution, exact UTF-8 stdout checks with CRLF normalization,
private module paths, unrelated cwd, failure diagnostics and artifact-absence
checks. An expected compilation rejection requires a normal nonzero exit and
nonempty stderr; crashes and timeouts are never accepted as rejection.

Each project is a separate stable CTest scenario with its own unique work
folder, a 120-second shared deadline and framework cleanup. Failures retain
commands, output and inputs; successful runs delete their work unless
`--keep-work` is used. The runner has no `assert`-based checks that Release can
disable. The existing host also keeps its assertions enabled in Release.

## Reproduction

```sh
cmake -S . -B build/provenance -DCMAKE_BUILD_TYPE=Debug -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE
cmake --build build/provenance --parallel 4 --target module_function_provenance_scenarios
ctest --test-dir build/provenance --output-on-failure -R '^integration.module_function_provenance\.'
build/provenance/module_function_provenance_scenarios integration.module_function_provenance.01-declaring-file build/provenance/module_import_execution_testing build/provenance test/data test/data --keep-work
# Replace the selected ID by --list to list all IDs.
```

Repeat with a separate Release directory. The last positional argument is a
fixture root, normally `test/data`, so negative tests can use a copied table
without editing source data. CMake can read the `id` lines from this same JSON
file; no second list of scenario names is needed.

## Verification before Python removal

- Debug, Release and UBSan: all 13 C scenarios passed, preserving all 36 host
  checks. UBSan uses `-fsanitize=undefined -fno-sanitize-recover=all`.
- The original Python runner passes the same Release host. An independent
  comparison of its executed fixture files with retained C workspaces found
  byte-identical `.goat` inputs and structurally identical `.gmod` descriptors
  for all 13 projects. C descriptor formatting alone differs from `json.dumps`.
- Release negative checks reject a deliberately wrong expected stdout; replacing
  the invalid import by valid source while still expecting failure also fails.
  Both failures retain scenario diagnostics. Malformed fixture JSON is rejected
  before dispatch. Positive `--keep-work` runs retain complete source trees.

To reproduce the original baseline, extract the former Python runner from the
provenance commit into a build directory, set `PYTHONPATH` to the repository's
`scripts/` directory for its unchanged shared execution helper, and run it with
the same `module_import_execution_testing` host. Normal CTest runs need no
Python or fixture generation.

## Complete scenario mapping

Each legacy project name is retained verbatim in the CTest suffix. All asserts,
imports/includes, positive observations and negative observations within that
project remain grouped together.

| Former project | Stable CTest ID | Checks | Preserved observations |
|---|---|---:|---|
| `01-declaring-file` | `integration.module_function_provenance.01-declaring-file` | 2 | Declaring-file observation and direct hidden-field denial |
| `02-callback` | `integration.module_function_provenance.02-callback` | 2 | Callback rights, exception unwinding and restored caller rights |
| `03-closure` | `integration.module_function_provenance.03-closure` | 2 | Escaped closures, independent captures and receiver storage |
| `04-method-storage` | `integration.module_function_provenance.04-method-storage` | 2 | Foreign method storage and owner-call denials |
| `05-function-reexport` | `integration.module_function_provenance.05-function-reexport` | 5 | Three-module function/object identity and closure reexport |
| `06-nested-export` | `integration.module_function_provenance.06-nested-export` | 3 | Nonrecursive export, nested objects/arrays and write denial |
| `07-return-object` | `integration.module_function_provenance.07-return-object` | 3 | Returned object identity without capability transfer |
| `08-constant-reference` | `integration.module_function_provenance.08-constant-reference` | 3 | Constant exported cell versus mutable array alias |
| `09-field-provenance` | `integration.module_function_provenance.09-field-provenance` | 3 | Value writes preserve file/module/visibility provenance |
| `10-replacement-function` | `integration.module_function_provenance.10-replacement-function` | 3 | Replacement function retains its own source-file rights |
| `11-independent-cells` | `integration.module_function_provenance.11-independent-cells` | 3 | Independent reexport cells, mutable aliases and later reassignment |
| `12-file-scope` | `integration.module_function_provenance.12-file-scope` | 3 | Explicit scope reexport, repeated include identity and hidden/public denial |
| `12-no-direct-internal-import` | `integration.module_function_provenance.12-no-direct-internal-import` | 2 | Direct import of a dependency internal source file is rejected |
