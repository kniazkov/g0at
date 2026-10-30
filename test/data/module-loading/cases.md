# Loading scenario map (#369)

Original `scripts/check_module_loading.py` has 52 checked groups, expanded into 94 independently repeatable CTest IDs. All original modes, faults, native-call markers, exit codes and JSON counters are retained.

| CTest suffix (`integration.module_loading.`) | Original family | Mode | Fault/kind |
|---|---|---|---|
| `off` | off | off |  |
| `valid-auto` | valid | auto |  |
| `valid-required` | valid | required |  |
| `no-match-auto` | no_match | auto |  |
| `no-match-required` | no_match | required |  |
| `plain-auto` | plain | auto |  |
| `plain-required` | plain | required |  |
| `foreign-auto` | foreign | auto |  |
| `foreign-required` | foreign | required |  |
| `ambiguous-auto` | ambiguous | auto |  |
| `ambiguous-required` | ambiguous | required |  |
| `ambiguous-off` | ambiguous | off |  |
| `damaged-off` | damaged | off |  |
| `damaged-auto` | damaged | auto |  |
| `damaged-required` | damaged | required |  |
| `os-auto` | bad_library | auto | os |
| `os-required` | bad_library | required | os |
| `descriptor-missing-auto` | bad_library | auto | descriptor-missing |
| `descriptor-missing-required` | bad_library | required | descriptor-missing |
| `descriptor-mismatch-auto` | bad_library | auto | descriptor-mismatch |
| `descriptor-mismatch-required` | bad_library | required | descriptor-mismatch |
| `descriptor-overflow-auto` | bad_library | auto | descriptor-overflow |
| `descriptor-overflow-required` | bad_library | required | descriptor-overflow |
| `descriptor-short-auto` | bad_library | auto | descriptor-short |
| `descriptor-short-required` | bad_library | required | descriptor-short |
| `descriptor-writable-auto` | bad_library | auto | descriptor-writable |
| `descriptor-writable-required` | bad_library | required | descriptor-writable |
| `symbol-missing-auto` | bad_library | auto | symbol-missing |
| `symbol-missing-required` | bad_library | required | symbol-missing |
| `bad-status-auto` | bad_abi | auto | bad-status |
| `bad-status-required` | bad_abi | required | bad-status |
| `bad-result-auto` | bad_abi | auto | bad-result |
| `bad-result-required` | bad_abi | required | bad-result |
| `write-before-auto` | write | auto | write-before |
| `write-before-required` | write | required | write-before |
| `write-partial-auto` | write | auto | write-partial |
| `write-partial-required` | write | required | write-partial |
| `cleaner-exit-auto` | cleaner | auto | exit |
| `cleaner-exit-required` | cleaner | required | exit |
| `cleaner-silent-auto` | cleaner | auto | silent |
| `cleaner-silent-required` | cleaner | required | silent |
| `real-producer-auto` | producer | auto |  |
| `real-producer-required` | producer | required |  |
| `unicode` | unicode | required |  |
| `allocated-string-auto` | string | auto | allocated-string |
| `allocated-string-required` | string | required | allocated-string |
| `static-string-auto` | string | auto | static-string |
| `static-string-required` | string | required | static-string |
| `dormant` | dormant | required |  |
| `concurrent-auto` | concurrent | auto |  |
| `concurrent-required` | concurrent | required |  |
| `dependency-valid-auto` | dependency_valid | auto |  |
| `dependency-valid-required` | dependency_valid | required |  |
| `import-only` | import_only | required |  |
| `dependency-only` | dependency_only | required |  |
| `dependency-bytecode` | dependency_optional | required | bytecode |
| `dependency-foreign` | dependency_optional | required | foreign |
| `dep-failure-os-auto` | dependency_failure | auto | os |
| `dep-failure-os-required` | dependency_failure | required | os |
| `dep-failure-symbol-missing-auto` | dependency_failure | auto | symbol-missing |
| `dep-failure-symbol-missing-required` | dependency_failure | required | symbol-missing |
| `dep-failure-descriptor-mismatch-auto` | dependency_failure | auto | descriptor-mismatch |
| `dep-failure-descriptor-mismatch-required` | dependency_failure | required | descriptor-mismatch |
| `foreign-corrupt-dependency-off` | dependency_corrupt | off |  |
| `foreign-corrupt-dependency-auto` | dependency_corrupt | auto |  |
| `foreign-corrupt-dependency-required` | dependency_corrupt | required |  |
| `isolated-write-before` | isolated | auto | write-before |
| `isolated-write-partial` | isolated | auto | write-partial |
| `global-exit` | global | auto | exit |
| `global-silent` | global | auto | silent |
| `integrity-auto` | integrity | auto |  |
| `integrity-required` | integrity | required |  |
| `all-symbols` | all_symbols | auto |  |
| `pending-exit-write-auto` | pending | auto | exit-write |
| `pending-exit-write-required` | pending | required | exit-write |
| `pending-exit-write-fail-auto` | pending | auto | exit-write-fail |
| `pending-exit-write-fail-required` | pending | required | exit-write-fail |
| `concurrent-write-fail` | concurrent_failure | auto | write-partial |
| `concurrent-cleaner-fail` | concurrent_failure | auto | silent |
| `caught-auto` | caught | auto |  |
| `caught-required` | caught | required |  |
| `frozen` | synchronized lifecycle | encoded in ID | independent process observation |
| `continued-auto` | synchronized lifecycle | encoded in ID | independent process observation |
| `continued-auto-exit` | synchronized lifecycle | encoded in ID | independent process observation |
| `continued-required` | synchronized lifecycle | encoded in ID | independent process observation |
| `continued-required-exit` | synchronized lifecycle | encoded in ID | independent process observation |
| `late-worker` | synchronized lifecycle | encoded in ID | independent process observation |
| `nonreturning-loop-auto` | synchronized lifecycle | encoded in ID | independent process observation |
| `nonreturning-loop-required` | synchronized lifecycle | encoded in ID | independent process observation |
| `nonreturning-block-auto` | synchronized lifecycle | encoded in ID | independent process observation |
| `nonreturning-block-required` | synchronized lifecycle | encoded in ID | independent process observation |
| `killed-owner` | synchronized lifecycle | encoded in ID | independent process observation |
| `held-auto` | synchronized lifecycle | encoded in ID | independent process observation |
| `held-required` | synchronized lifecycle | encoded in ID | independent process observation |
