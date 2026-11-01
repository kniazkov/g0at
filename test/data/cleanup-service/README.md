# Cleanup service acceptance migration (#376)

Runner: `test/src/functional/cleanup_service_scenarios.c`. Fixture remains
`test/src/component/cleanup_service_testing.c`, whose always-active CHECKs preserve
Release validation. No production behavior or policy changed.

Each expanded suffix below is a stable `integration.cleanup_service.<suffix>` ID
(36 IDs). The original serial acceptance script is preserved during the strict
original/C CI gate and removed only after that gate succeeds.

| CTest suffix | Original checks retained |
|---|---|
| `channel` | duplex large frames, read/write deadlines, peer close without SIGPIPE |
| `retry` | deterministic 30-second retry policy: immediate/retry/saturation |
| `lazy` | no cleaner PID or goat-run directory |
| `startup-{missing,exit,silent,late}` | four startup failures: shared sticky state, no native files, ack then owner exit; silent/late cleaner handle exit |
| `normal` | eight initial callers; short result/output, explicit idempotent finish, saved/old bytes preserved |
| `owner-{quit,exit}` | ordinary/23 exit triggers independent cleanup |
| `detached-client` | channel release not owner death; touch still works |
| `invalid-native-name` | POSIX invalid UTF-8 filename: finish4, done1, diagnostic directory retained |
| `killed-owner` | external owner termination triggers cleanup |
| `dead-cleaner-{0,23}` | READY retained, short communication error, user exit unchanged, directory retained |
| `active-compiler` | compiler/descendant pinned and stopped before directory cleanup |
| `child-deadline` | managed compiler deadline, ran0/3, finish0 and cleanup |
| `cleanup-race-00..07` | all eight finish/send versus owner death races |
| `launch-race-00..07` | all eight run/send versus owner death races |
| `busy` | Windows locked file: client5s timeout does not cancel same cleanup; unlock after owner exits |
| `permanently-busy` | Windows file remains locked through 30s failure, cleaner exits with done1; directory retained |
| `parallel-lifetime` | same long-lived parallel compiler/worker survives full original sequence, including both eight-race loops and Windows busy paths, then independently observed termination |

## Independence and bounds

The raw OS owner transport does not use the process supervisor under test and
never adopts cleaner descendants. Commands are actually flushed to the OS before
an immediate race kill. Cleaner/worker identities are pinned at live readiness
barriers; exit means pidfd/Windows handle readiness, exactly as the original
ProcessWatch (not PID nonexistence: an exited zombie satisfies this contract).

Normal line/owner/cleaner/worker/PID waits retain 10 seconds, startup failure lines
4 seconds, Windows busy response 8 seconds, persistent busy cleaner 28 seconds
after the owner's exit. The 40-second deferred failure cleanup is separate from
acceptance. Each focused scenario has a 60-second total deadline; parallel-lifetime
has 180 seconds; outer CTest watchdogs include bounded failure cleanup. Pinned
handles only are eligible for failure cleanup. Native invalid filenames and Windows
busy-file handles have deferred cleanup, including assertion failures.

Only invalid-native-name skips on Windows; only busy/permanently-busy skip on
POSIX. Linux/glibc and musl both execute all other cases. The parallel-lifetime ID
preserves the original **same** sentinel tree across unrelated cleanups instead of
replacing it with a fresh tree for each test. Focused IDs additionally isolate faults.

All work is under a unique build scenario directory with a Unicode subdirectory.
No test/data writes. Protocol lines strip only CR/LF (as original); persisted saved
bytes are exact. File and PID failures fail closed. Success removes work except
--keep-work; failure retains command.log, stdout/stderr, comparisons and execution.log.
The native invalid-byte file is removed only after the expected failure is observed,
so the UTF-8 scenario directory finalizer can remove the retained directory safely.

## Reproduce

```
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug --target cleanup_service_scenarios
ctest --test-dir build/debug -L cleanup-service-migration --output-on-failure
build/debug/cleanup_service_scenarios integration.cleanup_service.normal build/debug/cleanup_service_testing build/debug test/data --keep-work
```

Repeat with Release. Integration labels, both shell/cmd build target lists and
musl Debug/Release lists include the new target. CI links and final validation
results are maintained in the PR description, not an extra checklist-only push.

## Local evidence and explicit limits

Debug and Release each: 31 passed, 2 expected Windows-only skips, 3 failed
(active-compiler, child-deadline, parallel-lifetime). The unchanged original
Python script in each configuration reaches active-compiler then fails the same
retained-directory assertion. This executor lacks `/proc/thread-self/children`;
no cleanup assertion is weakened or marked skipped. Full strict remote parity is
required before removing Python. The original stops on first failure; a separately invoked unchanged child-deadline
block also fails with tree error 2 in both configurations. Later original blocks
are not claimed locally passed.

Negative controls use an independent external protocol fixture, not the production
service: positive baselines plus deliberate fixture exit, lazy line/PID/directory,
ready prefix/Unicode/multiplicity, short status/output, finish status, retained
directory, done status, saved/old bytes, owner exit and held-live-cleaner faults.
The held cleaner must fail at the original 10-second bound. Exact totals and
sanitizer/shared transport regression results are recorded in the PR.

The first remote parity run exposed a Windows-only oracle mismatch: GetTempPathW
supplies a trailing separator and the service's path join adds another separator.
Python Path normalizes this; the initial C byte-position check rejected it. The
comparison now normalizes repeated/trailing separators (plus Windows slash/case)
while still rejecting a different parent, nested descendants, `.` and `..`.
Twenty direct C predicate controls cover POSIX and Windows lexical branches.
