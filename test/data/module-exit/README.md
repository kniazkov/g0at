# Module return/exit migration (#377)

Runner: `test/src/functional/module_exit_scenarios.c`. Fixture:
`scenarios.json`, with provenance and original SHA-256 in `provenance.json`.
Each row is a stable `integration.module_exit.<suffix>` CTest ID, preserving the
entire original named project's ordered invocation matrix. There are 45 IDs,
50 module compile calls and 207 execution/public CLI calls (257 original subprocess
checks), plus one source-file write. No original Python expected value is generated
by production code; the corpus was recorded by replacing only the script's
execution boundary with literal-recording stubs. Re-running that recording yields
the same checked-in SHA-256. The production compiler still builds fresh artifacts
from those read-only source/expected inputs during each scenario.

| Original project / CTest suffix | Ordered matrix (arrow = exact expected OS exit) |
|---|---|
| `status-0` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `status-1` | compile app; component binary→1; CLI archive→1; CLI descriptor→1; source none/off→1; source all/off→1 |
| `status-7` | compile app; component binary→7; CLI archive→7; CLI descriptor→7; source none/off→7; source all/off→7 |
| `status-127` | compile app; component binary→127; CLI archive→127; CLI descriptor→127; source none/off→127; source all/off→127 |
| `status-128` | compile app; component binary→128; CLI archive→128; CLI descriptor→128; source none/off→128; source all/off→128 |
| `status-254` | compile app; component binary→254; CLI archive→254; CLI descriptor→254; source none/off→254; source all/off→254 |
| `status-255` | compile app; component binary→255; CLI archive→255; CLI descriptor→255; source none/off→255; source all/off→255 |
| `normal-no-unwind` | compile app; component binary→7; CLI archive→7; CLI descriptor→7; source none/off→7; source all/off→7; source all/auto→7 |
| `exit-in-finally` | compile app; component binary→9; CLI archive→9; CLI descriptor→9; source none/off→9; source all/off→9 |
| `exit-indirect-expression` | compile app; component binary→17; CLI archive→17; CLI descriptor→17; source none/off→17; source all/off→17; source all/auto→17 |
| `exit-method` | compile app; component binary→19; CLI archive→19; CLI descriptor→19; source none/off→19; source all/off→19 |
| `exit-shadowing` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `caught-invalid-function` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `exit-replaces-return` | compile app; component binary→11; CLI archive→11; CLI descriptor→11; source none/off→11; source all/off→11 |
| `cli-entry-points` | write main.goat; CLI compile→0; CLI legacy run→7; CLI save-native/nonexistent-report→7; interactive→7; no unreachable/stderr |
| `invalid-0` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `invalid-1` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `invalid-2` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `invalid-3` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `invalid-4` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `invalid-5` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `invalid-6` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `invalid-7` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `invalid-8` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `invalid-9` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `invalid-10` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `invalid-11` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `invalid-12` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `invalid-13` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `invalid-14` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `argument-evaluation` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `include-caught-then-exit` | compile app; component binary→7; CLI archive→7; CLI descriptor→7; source none/off→7; source all/off→7 |
| `callback-into-main` | compile app; component binary→7; CLI archive→7; CLI descriptor→7; source none/off→7; source all/off→7 |
| `include-sticky-failure` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `initializer-invalid-argument` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `initializer-finally-replaces-pending` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `import-caught-then-exit` | compile library; component binary→255; compile app; component binary→255 |
| `import-sticky-failure` | compile bad; compile app; component binary→0 |
| `transitive-dynamic-context` | compile leaf; compile outer; compile app; component binary→23 |
| `initializer-uncaught` | compile app; component binary→1; CLI archive→1; CLI descriptor→1; source none/off→1; source all/off→1 |
| `top-return-uncaught` | compile app; component binary→1; CLI archive→1; CLI descriptor→1; source none/off→1; source all/off→1 |
| `top-return-bare` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `import-return-bare` | compile bad; compile app; component binary→0 |
| `top-return-value` | compile app; component binary→0; CLI archive→0; CLI descriptor→0; source none/off→0; source all/off→0 |
| `import-return-value` | compile bad; compile app; component binary→0 |

## Contracts retained

- Statuses 0/1/7/127/128/254/255 retain exact OS identity. Normal exit bypasses
  catch/finally/host report writing; indirect expressions, methods, shadowing,
  invalid arguments, and pending return/finally interactions preserve output order.
- Each generic program compiles a module and executes component binary, public CLI
  ZIP archive, public descriptor and both source optimization modes. The two
  original native-auto variants remain enabled; none is silently downgraded to off.
- File/module initialization follows active dynamic context, including callbacks
  into main, transitive imported functions, sticky exception identity and repeated
  return/exit failures. Both bare and valued return cases remain separate.
- Public legacy compile/run/save-native and interactive entry points preserve exact
  exit codes; interactive output is checked for absence of unreachable execution and
  empty stderr (not compared to an invented fixed prompt).
- UTF-8 stdout is exact after CRLF-pair normalization. Explicit invocations require
  empty stderr or the original diagnostic substring. The original Project.run
  helper's success-stderr policy remains unspecified rather than introducing an
  unsupported new assertion.
- Each named project has independent workspace/module root and an unrelated cwd.
  The inherited per-project environment overrides only GOAT_MODULE_PATH. Interactive
  execution inherits the environment without that override, exactly as before.
- All commands retain the original 60-second limit via the test-only scoped
  `scenario_with_timeout` helper; the 240-second aggregate deadline cannot extend
  any command. Nested scopes take the minimum, restore on success, and abort the
  entire scenario on failure with normal resource cleanup. Outer CTest timeout is
  360 seconds including failure cleanup. Existing command deadline semantics are
  unchanged for callers not using the new helper.

## Reproduce

```
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug --target module_exit_scenarios
ctest --test-dir build/debug -L module-exit-migration --output-on-failure
build/debug/module_exit_scenarios integration.module_exit.status-255 build/debug/module_import_execution_testing build/debug/goat build/debug test/data test/data/module-exit --keep-work
```

Repeat Release and target-platform CI. No platform skips are expected. Inputs under
`test/data` are read-only; `.gitattributes -text` protects the frozen JSON checksum.
Each scenario creates a unique build directory. Failures retain full command stdout,
stderr, argv and exact comparisons; successful work is removed unless --keep-work.

## Local evidence

Debug/Release each pass all 45 C IDs, and unchanged original Python passes all 257
checks in each configuration. Independent negative controls: 22/22 across both
configurations, comprising three positive baselines plus incorrect exact status,
stdout, diagnostic, interactive unreachable execution, corrupt source, malformed
status type, unknown operation and unexpected stderr. These use actual fresh
compilation/execution, with one external stderr-injecting host wrapper.

Seven permanent `framework.execution.scoped-*` controls cover shorter deadline,
no extension, restoration, nested caps, invalid zero/null inputs and an expired
outer deadline before body entry. Both success and expected-failure paths verify
exactly-once deferred cleanup; failed cases verify the diagnostic and remove their
own retained work. Debug/Release pass all 14 invocations.

Full original/C remote parity and final Python-removal CI are recorded in the PR;
The original Python was removed after that parity gate passed. This task does not alter language
semantics or production exit behavior.
