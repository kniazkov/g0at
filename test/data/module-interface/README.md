# Module interface scenarios (#362)

`scenarios.json` preserves the exact source strings, expected stdout, command
ordering, expected failures, and source/artifact removals from
`scripts/check_module_interface.py` at baseline
`913d87b8c4f8affc7c5d705f3c82b3f72684f1cc`. The original Python checker was run
successfully while recording these inputs: 13 projects and 52 compile/VM checks.
The fixture is data only: metadata and diagnostic assertions are implemented in
`test/src/functional/module_interface_scenarios.c`, not delegated to Python.

Each original `Project` maps to one independently selectable CTest scenario:

| Original project / ID suffix | Preserved contract |
| --- | --- |
| `scalars` | Exact integer boundaries, embedded NUL/empty strings, boolean, real bits including signed zero, character/null, visibility and mutability |
| `function-identities` | Stable function IDs, file IDs, parameters, aliases and mutable function boundary |
| `metadata-consumer` | Exported summaries and metadata-only imported scalar/function inference |
| `reexports` | Original-owner function references and immediate dependency metadata |
| `proof-boundaries` | No nested/non-main exports or unsupported function summaries |
| `mutable-function-contract` | Mutable imported functions never provide stale compile-time results |
| `constant-function-effects` | External read/write effects and no stale mutable-state return value |
| `callback-contract` | Unknown callback effects, VM execution and throwing callback stdout |
| `result-alias-contract` | Returned object aliases cannot freeze mutated values |
| `nested-sharing-contract` | Nested shared object mutation cannot freeze values |
| `static-call-diagnostics` | Direct/aliased non-callables diagnosed; dynamic mutation not incorrectly diagnosed |
| `instrumented-metadata-contract` | Linker-wrap analysis/interpretation/execution counters and compilation/loading after source removal |
| `initialization-preserved` | Runtime dependency initialization, including failure stdout |

The full ID is `integration.module_interface.<suffix>`. Metadata is read using the
independent ZIP oracle and yyjson; no production loader supplies the test's
expected metadata. The instrumented host commands retain their exact counter
stdout, including zero execution during compile/load and two executions for the
final binary. Source removal occurs at its original position, before downstream
compilation or binary execution, rather than merely during test cleanup.

Run a scenario after configuring/building the project:

```sh
ctest --test-dir build/Debug -R '^integration\.module_interface\.' --output-on-failure
build/Debug/module_interface_scenarios \
  integration.module_interface.instrumented-metadata-contract \
  build/Debug/module_import_execution_testing build/Debug test/data test/data --keep-work
```

Use the corresponding Release build for the same cases. Each scenario gets an
isolated workspace and a 120-second deadline; `--keep-work` preserves failed
inputs and outputs for diagnosis. Replacing the ID with `--list` lists IDs.

Retrieve the original checker from baseline `913d87b8` to reproduce parity by running
`python3 scripts/check_module_interface.py BUILD/module_import_execution_testing`
and then all 13 C scenarios against that same host. Negative validation should
alter a copied fixture's expected stdout (never the canonical fixture) or use a
host that returns incorrect metadata/counters, and require the scenario to fail.

Local validation during migration: all 13 C scenarios pass with both Debug and Release
hosts (the original Python reports 52 checks for each configuration). Copied-fixture negative controls are
rejected in both configurations for a changed exact scalar value, incorrect compile-contract counter
stdout, incorrect failure stdout, and replacing a proven non-callable diagnostic
case with a dynamic call. These controls exercise independent metadata checks,
exact command output, expected failure handling, and diagnostic requirements.
