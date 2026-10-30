# Real lazy native loading (#369)

The independent fixture builder is `test/support/module_loading_fixture.c`;
normal contracts are in `test/src/functional/module_loading_cases.c` and synchronized
lifetime cases in `module_loading_lifecycle.c`. [All 94 CTest IDs](cases.md) map the
52 groups of the former `scripts/check_module_loading.py`. Sources and custom native
fixture strings are exact C literals; generated archives/libraries stay in each
unique scenario directory, never in this read-only documentation directory.

## Reproduction

```
cmake -S . -B build/loading -DCMAKE_BUILD_TYPE=Debug -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE
cmake --build build/loading --parallel 4 --target module_loading_scenarios
ctest --test-dir build/loading -L module-loading-migration --parallel 4 --output-on-failure
```

Repeat with Release. Every case is individually selectable. Direct example:

```
build/loading/module_loading_scenarios integration.module_loading.killed-owner \
  build/loading/module_loading_testing build/loading/module_import_execution_testing \
  cc build/loading test/data include docs/module-format-v1/schema-source.json --keep-work
```

All cases have a 300-second shared scenario deadline (CTest adds a 30-second cleanup
margin). Full stdout/stderr, fixture compiler commands, generated sources and
archives remain on failure; successful directories are removed unless `--keep-work`.
There are no platform skips. Linux x86-64/AArch64, Windows MinGW32/MinGW64/UCRT64 and
both real-musl jobs run Debug and Release.

## Independence and cleanup

The C fixture code uses the independent test format oracle for canonical metadata,
SHA-256/main hashes and ZIP writing. It does not use the production writer to repair
corrupt fixtures. Actual native libraries are compiled and loaded; descriptor errors,
all-symbol validation, concurrent entry and noncooperative DLL/SO calls remain real.

Owner transport uses raw OS pipes/process handles in test support, not the production
process supervisor: that supervisor would adopt and terminate the detached cleaner
whose independent lifetime the scenario is measuring. Linux pidfds / Windows process
handles in `process_watch` pin owner/cleaner identities after explicit READY/HOLD
barriers. Process death is verified independently, never inferred from a PID or delay.
Scenario cleanup closes pipes, reaps owners and closes pinned observers even on failure.
Unobserved cleaner PIDs are never reopened from stale marker files: unpinned services
follow their original owner-death protocol, while explicitly pinned lifecycle services
are waited independently before handles are closed.
The only host changes keep side-effectful assertions active in Release and emit a
test-only owner PID marker after setup; production behavior is unchanged.

## Verification and negative controls

The original Python runner passed all 52 groups on the same final Debug and Release
hosts. Independent Python regeneration compared 21 custom native source token streams
and exact descriptor blobs; 84 archive hashes (including two deliberately stale
hashes) and seven metadata/payload mutation classes were checked independently.

Temporary host proxies exercise nine negative controls in Debug and Release: wrong
stdout, malformed RESULT JSON, wrong native-call marker bytes, wrong write count,
false loaded status, wrong native-load count, duplicate RESULT records, 256-KiB stdout/stderr, and an owner that exits before its READY barrier. They must each fail an intended
assertion and retain diagnostics. The proxies are verification tools, not runtime
Python dependencies. Full C scenarios are run with Python discovery disabled.
