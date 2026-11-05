# Manifest-driven native functional runner (#383)

## Mapping and direct use

The 22 entries in `test/data/native/list.json` map to
`integration.native_functional.<name>`; `main-mapping.json` preserves the original
manifest objects, stable IDs, adapter kinds and ordered command labels. In #394
only the six active `check` values changed from legacy script paths to built-in
IDs; names, program entries and command sequences are unchanged.

- **16 native/VM programs:** access, bool_char, add_specializations, string_methods,
  string_identity, string_lifetime, string_nested, string_concat, string_chain,
  quadratic, string_conversions, quadratic_repeated, loops, switch, conditional,
  compound.
- **Five C check adapters (#384/#385/#386):** access_guards, libm, runtime_strings,
  string_method_errors and saturation route built-in check IDs to
  `native_access_scenarios all`, `native_libm_scenarios all`,
  `native_runtime_strings_scenarios all`, `native_string_method_scenarios all` and
  `native_saturation_scenarios all`.
  Targets live beside the required policy selftest, keep the original outer
  300-second deadline, and receive the same Goat/repository/data paths.
- **One C selftest adapter:** runner. The built-in manifest check ID `runner`
  maps to `native_runner_policy_testing all`; both existence validation and
  execution use that C executable, never the replaced Python selftest.

```
cmake --build BUILD --target native_runner_testing native_runner_policy_testing native_access_scenarios native_libm_scenarios native_runtime_strings_scenarios native_string_method_scenarios native_saturation_scenarios
ctest --test-dir BUILD -R '^integration.native_functional\.' --output-on-failure
native_runner_testing all GOAT SELFTEST REPOSITORY BUILD DATA --only access bool_char
native_runner_testing integration.native_functional.access GOAT SELFTEST REPOSITORY BUILD DATA [--keep-work]
```

`DATA` is the repository's `test/data`. `--list` prints stable IDs. The whole
manifest is validated before selection, including unselected files/fields.
Only `runner`, `access`, `libm`, `runtime_strings`, `string_method_errors` and
`saturation` are accepted as check IDs. Unknown IDs, executable/script paths and
embedded NULs are rejected even if the requested file exists. No interpreter
argument or fallback execution path remains.
`--only` rejects unknown names before any case runs, preserves manifest order and
deduplicates requested names. Every case has its own fail-fast scenario; a failed
case does not prevent subsequent selected cases from running. JSON duplicate
object keys use Python's last-value semantics throughout main and shared policy.

Each case owns `BUILD/native-functional/scenario-*`; `case.name` identifies it.
Unlike most other migrated suites, **successful diagnostics remain by default**,
preserving the original runner's deliberate retention policy. `--keep-work` is
accepted. No previous exported SDK can leak into a new exclusive workspace.

## Preserved execution and oracle policies

The 16 program cases retain **76 ordered external commands**: four per case
(VM, native, compile, saved), six generated-C checks, and two driver cases with
export/compiler/driver steps. The six adapter entries add six external commands.
Every command retains the original 300-second limit and repository-root cwd.
The maximum eight-command case has a 2400-second aggregate budget. An `all` run
is sequential with an independent bounded budget per case, not an unbounded
single scenario.

- The VM uses `--optimize none --native off`; native execution is required.
- All commands require normal status zero. Goat commands additionally require
  empty stderr; compiler, driver and check commands retain unrestricted binary
  stdout/stderr on successful status.
- Shared `nr_output_equal` compares binary output with CRLF pairs normalized only:
  no trimming, NUL removal or extra/missing empty-line acceptance.
- Shared `nr_check_report` checks exact mode/attempts/succeeded/retries strings,
  positive native calls, ready preparation, positive bound and optional exact
  bound. The same implementation is exercised by the new C selftests.
- Optional expected output, source/example equality, normalized output substrings,
  strict-UTF-8 generated C and nonoverlapping pattern counts are retained. Empty
  patterns count decoded code points rather than UTF-8 bytes.
- Drivers compile the existing real driver source, exported C/SDK archive,
  `native_services.c`, Unicode and allocator sources with the original warning,
  include, memory-debug and math flags. `CC` remains the raw inherited value, or
  `gcc`; it is not rewritten through a different compiler-selection policy.
- Compilation must print nothing. Then `program.goat` is actually deleted and the
  persisted `.gbin` is run with `CC` pointing at a nonexistent work-file path.
  Saved native output and report must still match the VM and exact call counts.

Original `label.command.json`, `label.stdout` and `label.stderr` diagnostics are
retained alongside framework `command-N` captures. JSON stores the same argv
array (formatting is not an oracle). A deferred copy preserves partial binary
streams under legacy labels even when timeout or process failure aborts the body.

## Compatibility boundary and provenance

Existing native sources, expected output, examples and drivers are unchanged.
The active manifest is consumed read-only at runtime; no oracle fixture is regenerated. `main-provenance.json`
records original source/manifest hashes and coverage counts.

Full remote parity passed with the original scripts unchanged. The Python
manifest runner and `test/src/native/checks/runner.py` were removed in #383.
The temporary `scripts/check_native.py` execution/output helper remained for
`string_method_errors.py`, then was removed with its final consumer after the
complete #385 parity gate. Saturation followed after the complete #386 parity
gate in [PR #464](https://github.com/kniazkov/g0at/pull/464). All check-program
adapters now route to C scenario runners. In #394 the active manifest uses
built-in check IDs and rejects script paths. Frozen `main-mapping.json` and
provenance retain the original logical check names as historical evidence.

## Local evidence and limitations

- Unchanged Python: **22/22** in Debug and Release.
- C real suite: **6/22 Debug**, **8/22 Release**. All 16/14 failures report
  `status=CHILD_EXITED exit=0 signal=0 system=0 cleanup=2`; none is hidden or
  converted to success. Local full C/Python parity is **not achieved**.
- `cleanup=2` is native `ENOENT`, not a cleanup-status enum. The existing Linux
  supervisor's `kill_adopted()` opens `/proc/thread-self/children`, returning
  errno through `cleanup_tree()` and `reply.cleanup` to `cleanup_error`.
  Directly opening that path here returns `ENOENT`; this matches the recorded
  failure path. `scenario_command_capture` correctly rejects nonzero cleanup
  errors. The environment denies ptrace (`PTRACE_TRACEME`/`PTRACE_SETOPTIONS`), so
  per-failure syscall tracing was unavailable; no permission bypass was attempted.
- **34/34 independent native-C fake end-to-end controls** in Debug/Release cover
  the actual main command flow, reports, status/stderr/output failures, generated
  patterns/UTF-8, Unicode empty-pattern count, shadowed duplicate JSON keys,
  driver/compiler failures, source deletion and missing-CC saved execution,
  whole-manifest validation, selection ordering/deduplication, failure
  continuation, and C selftest routing without the old Python selftest file.
- **Two additional actual `execute()` timeout controls** pass in Debug/Release:
  an outer test-only 300-ms budget clips the unchanged 300-second command budget;
  partial stdout containing NUL/CRLF and partial stderr are retained exactly in
  both generic and legacy-labelled captures, and the child is cleaned up.
- Shared-policy/selftest results are documented in [README.md](README.md).
  Formatting and `git diff --check` pass.

Full remote original/C parity on the supported platforms is mandatory before
Python deletion. No production or cleanup check was weakened for local success.

## Remote parity and removal

Parity head `7dd605b0a47e541e4eedb72c40d229de75ac5186` passed
[Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41495133812) and
[vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41495133951).
The initial musl AArch64 saturation adapter failure did not reproduce after
adding diagnostic retention only; its exact cause remains unproven. No assertion
or deadline was relaxed. Python removal follows this complete gate; final CI
must still verify the removal commit.


### Saturation adapter retirement (#386)

Parity head `38a029e15a4fab21942a3782dc14ef92b3d48892` passed
[Build 11/11, attempt 4](https://github.com/kniazkov/g0at/actions/runs/41623617966/attempts/4)
and [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41623617722)
with the original Python checker and original/C wrapper still present. Only
then were they removed and the logical saturation adapter routed to C.
The C aggregate preserves its 300-second budget and original 120-second ordinary
and 30-second invalid-command caps. See [saturation coverage and evidence](../native-saturation/README.md).
Final-removal CI must still validate the retirement commit. Earlier intermittent
GCC builtin-driver compilation at the 120-second cap is not claimed fixed; its
deeper cause remains unknown.

### Final native Python adapter cleanup (#394)

All six manifest check consumers had migrated to C before this cleanup. The
unused Python argument and fallback were removed together with the legacy
script-shaped IDs. `nr_check_kind` defines the fixed C check namespace and
manifest validation rejects unknown checks before any selected case runs.
All 22 functional and 28 pre-existing policy IDs remain. Two additional controls,
`helpers.native_runner.manifest-builtins` and
`helpers.native_runner.manifest-reject-check`, cover all six accepted IDs, missing
executables, legacy paths, unknown IDs, absolute/relative paths, wrong types,
embedded NUL and last-duplicate-key behavior.

Reproduce the changed contract without Python discovery:

```sh
cmake -S . -B build/native-c-only -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE
cmake --build build/native-c-only --target native_runner_testing
ctest --test-dir build/native-c-only -R '^helpers.native_runner\.' --output-on-failure
ctest --test-dir build/native-c-only -R '^integration.native_functional\.' --output-on-failure
```

Local Debug and Release policy coverage passes 30/30 each with Python discovery
disabled. Old/new `--list` output matches for all 22 IDs; captured check commands
match for all six routes after normalizing build/work paths. Four direct negative
checks reject invalid unselected manifest entries before execution or workspace
creation. The unchanged pre-cleanup runner
and new runner both execute all six C check routes in manifest order. The
selftest route passes in both. Five baseline and four new native routes report
the existing intermittent local `cleanup=2` limitation after successful child
exit; `runtime_strings` passes in the new run. This is not claimed as passing
full native parity; supported remote CI remains required.
