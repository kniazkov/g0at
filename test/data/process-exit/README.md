# Process exit migration (#377)

Runner `test/src/functional/process_exit_scenarios.c` replaces
`scripts/check_process_exit.py` after strict original/C parity. All 24 original
host invocations are preserved under 22 stable CTest IDs; fast-three intentionally
keeps the cross-run minimum assertion in one independently selectable scenario.
No frozen random inputs are needed: original literal bytes and exact statuses are
read-only C constants, and generated readiness markers live only below build.

| `integration.process_exit.` suffix | Original probe and assertions |
|---|---|
| arbiter | normal host arbiter, exit0, 10s |
| unprepared | normal host unprepared, exact83, 5s |
| prepare-race | eight preparers, exit0, 10s |
| failed-prepare | failure host, exact91, 5s |
| delayed-prepare | delayed host, exact91, 8s watchdog, 0.75 <= elapsed < 5s |
| fast-three | three fast runs, exact255 and complete stdout/stderr literals each; min elapsed < 0.9s |
| error | closed underlying descriptor, exact77, no CRT finalization |
| pipe-error | close parent stdout reader before sending release, exact77, no CRT finalization |
| write-stdout, write-stderr, write-both | unread pipes deliberately remain full; exact73 and 0.75 <= elapsed < 5s |
| lock-stdout, lock-stderr | permanently held FILE lock; exact73 and same 0.75–5s bound |
| shared-budget | release stdout at 0.8s after readiness, stderr remains locked; exact73, 0.75 <= elapsed < 1.65s |
| race-00 through race-07 | all eight arbitration repetitions, only exact0 or7 accepted |

Every coordinated invocation requires a real readiness marker within15s. The
marker is checked before owner state exactly as the old runner; scheduler delay
never proves readiness. The owner receives one `x` byte and must exit within8s.
`ATEXIT` is forbidden in **concatenated** stdout+stderr, including a marker split
across the stream boundary. Fast expected bytes are not CRLF-normalized.

## Deliberately independent transport

No production child launcher, process exit arbiter, or continuously draining
module owner transport controls the host. POSIX uses fork/exec, native pipes and
an input socket; Windows uses CreateProcessW with an explicit inherited-handle
list and pipes. The input socket carries exactly one byte to getchar and avoids
changing the test process's global SIGPIPE policy. Neither output pipe is read
until waitpid/Windows process-handle observation proves the owner exited.
Draining before exit would hide blocked flush bugs and is expressly forbidden.

Failure cleanup kills/reaps only the launcher's retained child identity within8s,
then preserves available output without unblocking a live host. Native process,
pipe and log handles have scenario-deferred cleanup. Successful complete output,
mode/status/timing logs and diagnostics live under a unique scenario directory.
The scenario has a40s overall budget (90s for fast-three); CTest adds a cleanup
margin. Original 5/8/10/15s operation bounds are not expanded. No platform skips.
RUN_SERIAL retains isolation for timing-sensitive scenarios, as the old suite.

The existing fixture used side-effectful assertions for argument decoding,
thread creation and ready markers. A test-only NDEBUG guard preserves those
operations and assertions in all three Release host variants; production build
flags and exit behavior are unchanged.

## Direct execution

```
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug --target process_exit_scenarios
ctest --test-dir build/debug -R '^integration.process_exit\.' --output-on-failure
build/debug/process_exit_scenarios integration.process_exit.fast-three build/debug/process_exit_testing build/debug/process_exit_failure_testing build/debug/process_exit_delayed_testing build/debug test/data --keep-work
```

Repeat with Release. Exact local/parity/negative-control and final CI evidence is
recorded in the containing PR; original Python was removed after the full parity gate passed.
