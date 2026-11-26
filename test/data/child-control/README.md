# Supervisor reply investigation (#645)

25 November 2026, Linux x86-64, GCC 14.2.0, Debug, based on master `735781da`.
The test compiler is the host GCC; fixture faults are compile-time only.

The adjacent `linux-debug-faults.txt` records eight deterministic scenarios.
Raw supervisor wait status is meaningful only if `reaped=1`: a nonblocking
sample can precede supervisor exit. The test intentionally permits that race.

Without the EINTR retry loop, the `eintr` case exits with SIGABRT at the
successful-child assertion; after restoring the loop all eight cases pass.
This is a deterministic negative control, not a reproduction of the original
intermittent GitHub incident. The discarded reply mechanism is established;
the historical signal/errno was not recorded and remains unknown.

`linux-debug-native-runs.json` contains 3 isolated and 9 concurrent executions
(three at a time, distinct working directories) of the complete
`test/string_builder_modes.cmake` workload. All 12 passed, approximately
14.9–15.8 seconds each. This is evidence of tested stability, not proof of the
old failure's cause or its absence under other scheduling conditions.

Reproduce each native run:

```sh
cmake -DGOAT=/absolute/build/goat -DDATA=/absolute/g0at/test/data/functional \
  -DWORK=/absolute/unique-work-directory -P test/string_builder_modes.cmake
```

Local validation: Debug and Release process/toolchain/requirements selection,
198 passed plus the existing non-musl-host skip (199 registered); ASan/UBSan
transport fixtures: 8 cases, 25 consecutive runs each, LSan disabled. Debug
GC acceptance is also run after changing the internal child-result structure.
Doxygen and formatting are checked separately.

The PR re-enables the existing diagnostic workflow for changes to the affected
files: 12 configurations, 50 native workloads each and 25 repetitions per fault.
No failed attempt is discarded or replaced by a successful retry. CI evidence
is pending; #645 and the overall GC closure must not be marked complete solely
from the successful local runs.

## Startup diagnostics follow-up, 26 November 2026

Base: master `3ab8caffe5a8fed08abb6527431e355d567eed59` (PR #660 merged).
Linux x86-64 GCC 14.2.0; Debug, Release and Debug ASan/UBSan (LSan off,
`-fno-pie` / `-no-pie` for local sanitizer executables).

Final `ctest -L child-control --repeat until-fail:25 --output-on-failure`:
24 cases × 25 repetitions × 3 configurations = 1800 passed launches.
Debug service/native-failure selection: 64 registered, 62 passed, two existing
Windows-only busy-file tests skipped. The full StringBuilder modes script passed
with a fresh work directory. clang-format 18.1.8 and Doxygen 1.9.8 checks passed.
The CI matrix is pending and is not replaced by these local results.

The first version of the new timeout fixture exited at its own startup deadline.
Release and sanitizer repeats caught EOF racing the timeout (not a production
regression and not a reproduction of #659):

```
Release: startup status=2 stage=2 transport=2 elapsed_ms=1001 type=0 size=0
ASan:    startup status=2 stage=2 transport=2 elapsed_ms=1003 type=0 size=0
Assertion `d.stage == stage && d.transport == transport' failed.
```

The deterministic fixture now holds its channel open until owner death. The
startup deadline and production transport are unchanged. Final counts above
refer to that corrected fixture; the failed initial repeats are not counted.

Negative control: replace the READY-body stage assignment with HEADER, rebuild
Debug fixture and run `child_control_testing body startup`:

```
startup status=1 stage=2 transport=2 elapsed_ms=2 type=1 size=6
Assertion `d.stage == stage && d.transport == transport' failed.
returncode: -6
```

Restore the assignment and rebuild: the same test passes with
`status=1 stage=3 transport=2 type=1 size=6`. This proves the test detects loss of
stage evidence; it does not prove the cause of either historical incident.

## Non-consuming observation follow-up, 26 November 2026

Base: master `6fc5ff1a32dfdff034a5564958e5e0e4b9959096` (PR #661 merged).
The new observation is deliberately separate from poll: it does not enforce
an expired command deadline or wait for tree cleanup. It preserves the later
wait result and output, including callbacks. Linux fault tests observe twice
before wait; cross-platform tests cover running/missing/exited children.

Negative control: remove `MSG_PEEK` from the Linux observation and rebuild the
Debug fixture. `cleanup_service_testing observe exited` exits 1:

```
check failed: repeated.status == sample.status && repeated.exit_code == sample.exit_code && repeated.system_error == sample.system_error
```

Restoring MSG_PEEK and rebuilding makes the same test pass. This checks the
non-consuming contract, not the cause of the historical intermittent failure.

Initial Release and sanitizer repeats failed the new live-child test at
`sample.status == CHILD_RUNNING && sample.exit_code == -1`. That fixture used
a 1 ms **launch** deadline, which can expire in start_child_process before it
launches anything. The corrected fixture allows 1000 ms for launch, checks the
initial observation, then waits past that same deadline before observing again.
This changes the test only; production deadlines and behavior are unchanged.
The initial failed repeats are excluded from final success counts.

Example startup observations on this machine:

```
missing: [status=1 stage=2 transport=2 elapsed_ms=1 type=0 size=0] [process=0 exit=-1 signal=0 system=0 control=0]
exit:    [status=1 stage=2 transport=2 elapsed_ms=1 type=0 size=0] [process=0 exit=-1 signal=0 system=0 control=0]
timeout: [status=2 stage=2 transport=1 elapsed_ms=1002 type=0 size=0] [process=0 exit=-1 signal=0 system=0 control=0]
```

These intentionally preserve unavailable evidence: the supervisor's terminal
packet had not arrived at the one startup sample. Dedicated observation tests
wait for a packet under their own test budget and then require exact exit/error
classification and unchanged subsequent wait. Production does not add that wait.

Final local validation (Linux x86-64 GCC 14.2): 28 cases × 25 repeats ×
Debug/Release/ASan+UBSan = 2100 passed (LSan off, local sanitizer non-PIE).
The 40 service/native-failure scenarios had 38 passes and the two existing
Windows-only busy-file skips. Full StringBuilder modes passed with a new work
directory. clang-format 18.1.8, Doxygen 1.9.8 and diff checks passed. Windows
behavior awaits CI; successful local repeats do not close #645 or #659.

### Windows text-mode fixture correction in PR #662

CI run 45815922620 failed only the `child_observation.exited` byte-count check
on MINGW64, UCRT64 and MINGW32 (jobs 139895506912, 139895506940, 139895507066).
The fixture wrote `observation\n` with fwrite to text-mode stdout; Windows
expands LF to CRLF, while the assertion expected 12 bytes and callback bytes.
The fixture now uses a shared newline-free payload and sizeof-based lengths.
Exact byte comparison and callback accounting remain required. No production
process or cleanup behavior changed. All 12 Linux investigation jobs succeeded
in run 45815922588; this Windows failure does not explain #645/#659.
