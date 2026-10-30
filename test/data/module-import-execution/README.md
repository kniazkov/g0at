# Import execution scenario migration (#362)

`scenarios.json` is the exact source/expected-output table from
`scripts/check_module_import_execution.py` at parent `913d87b8`.
Each original Project is an independent CTest ID (33 IDs, 95 compile/VM calls).
The real module host is used, never a simulated evaluator. Source bytes, library
versions, dependency defaults, operation order, stdout and failure statuses match
Python. Runtime modules retain their source snapshots; the frozen scenario deletes
the complete project only after the child has signalled READY.

| Original Project | CTest ID | Host calls |
| --- | --- | ---: |
| `identity-versions` | `integration.module_import_execution.identity-versions` | 4 |
| `nonzero-main` | `integration.module_import_execution.nonzero-main` | 3 |
| `transitive` | `integration.module_import_execution.transitive` | 5 |
| `defaults` | `integration.module_import_execution.defaults` | 5 |
| `cross-module-context` | `integration.module_import_execution.cross-module-context` | 3 |
| `lexical-and-lazy` | `integration.module_import_execution.lexical-and-lazy` | 4 |
| `nested-initialization` | `integration.module_import_execution.nested-initialization` | 4 |
| `expression-and-include` | `integration.module_import_execution.expression-and-include` | 3 |
| `failed-repeat` | `integration.module_import_execution.failed-repeat` | 3 |
| `uncaught-root-failure` | `integration.module_import_execution.uncaught-root-failure` | 3 |
| `function-exception` | `integration.module_import_execution.function-exception` | 3 |
| `callback-import-finally` | `integration.module_import_execution.callback-import-finally` | 4 |
| `nested-failure` | `integration.module_import_execution.nested-failure` | 4 |
| `invalid-0` | `integration.module_import_execution.invalid-0` | 2 |
| `invalid-1` | `integration.module_import_execution.invalid-1` | 2 |
| `invalid-2` | `integration.module_import_execution.invalid-2` | 2 |
| `invalid-3` | `integration.module_import_execution.invalid-3` | 2 |
| `invalid-4` | `integration.module_import_execution.invalid-4` | 2 |
| `invalid-5` | `integration.module_import_execution.invalid-5` | 2 |
| `invalid-6` | `integration.module_import_execution.invalid-6` | 2 |
| `invalid-7` | `integration.module_import_execution.invalid-7` | 2 |
| `invalid-8` | `integration.module_import_execution.invalid-8` | 2 |
| `invalid-9` | `integration.module_import_execution.invalid-9` | 2 |
| `invalid-10` | `integration.module_import_execution.invalid-10` | 2 |
| `invalid-11` | `integration.module_import_execution.invalid-11` | 2 |
| `invalid-12` | `integration.module_import_execution.invalid-12` | 2 |
| `invalid-13` | `integration.module_import_execution.invalid-13` | 2 |
| `direct-source` | `integration.module_import_execution.direct-source` | 3 |
| `build-identity-cycle` | `integration.module_import_execution.build-identity-cycle` | 2 |
| `preflight-missing` | `integration.module_import_execution.preflight-missing` | 3 |
| `preflight-corrupt` | `integration.module_import_execution.preflight-corrupt` | 3 |
| `preflight-changed` | `integration.module_import_execution.preflight-changed` | 4 |
| `frozen` | `integration.module_import_execution.frozen` | 4 |

## Checks not expressible as stdout alone

- `identity-versions` checks exact ordered name/version dependency identities;
  `transitive` checks ordered direct dependency names.
- `nonzero-main` independently reads/rebuilds the ZIP, changes the main file index
  to 1 and exported integer to 42, and recomputes mainHash with the test oracle.
  It proves the VM does not accidentally execute file 0.
- The 14 invalid imports fail compilation without publishing an artifact.
  `build-identity-cycle` also checks no pending artifact remains.
- The three preflight cases remove, corrupt, or replace a referenced library;
  execution must fail with empty stdout, even though the import is unreachable.
- `frozen` uses the shared child-process barrier (including Windows CRLF READY),
  deletes all inputs, then checks exact Unicode stdout and successful exit.
- All host invocations use unrelated working directories and scoped search paths;
  shared helpers preserve crash/exit checks, exact stdout, timeouts and diagnostics.

## Reproduce

```sh
cmake -S . -B build/check -DCMAKE_BUILD_TYPE=Debug
cmake --build build/check --target module_import_execution_scenarios module_interface_scenarios
ctest --test-dir build/check -L module-execution-interface-migration --output-on-failure
build/check/module_import_execution_scenarios integration.module_import_execution.frozen \
  build/check/module_import_execution_testing build/check test/data test/data \
  docs/module-format-v1/schema-source.json --keep-work
```

Repeat with `Release`; CI runs both configurations on Linux/glibc, Linux/musl,
and Windows. The two original Python files can be obtained from parent
`913d87b8` for parity reproduction. Before removal both Python configurations
passed 95 import checks, and the C IDs passed the same inputs. The shared Python
`module_execution_support.py` remains for unmigrated consumers (#363 and later).

## Negative runner checks

Copy the fixture directory to a temporary location and pass it as FIXTURES above.
For `identity-versions`, independently change an invoke's expected stdout, change
the first expected dependency identity, or replace a mode with `bogus`.
Each change must return nonzero, identify the assertion, and retain its workspace.
Do not modify the production fixture. These mutations were verified during this
migration. Existing negative compile/load/VM cases are also registered normally.
