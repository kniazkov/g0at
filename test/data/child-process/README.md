# Independent child-process scenarios (#375)

The 55 `integration.child_process.*` CTest IDs migrate the contracts in
`scripts/check_child_process.py` (original SHA-256 recorded in `provenance.json`).
Every invocation has its own build-local workspace, full stdout/stderr artifacts,
shared scenario deadline and independent OS transport/watchdog. `--keep-work`
preserves the raw evidence. No production process supervisor launches the fixture:
it must not adopt, kill or reap the cleanup service being tested.

## Original assertion mapping

All names below have the prefix `integration.child_process.`.

| Scenario suffix | Original lines | Preserved contract |
| --- | --- | --- |
| `empty`, `exit`, `stdin`, `closed-input`, `inherit`, `parent` | 28–35 | Normal status/exact code; empty captures/untruncated flags; parent's Q input |
| `stdin-open-pipe` | 38–49 | Fixture completes while parent's writer remains open; Q preserved |
| `stdin-file` | 50–56 | Real regular-file input `Qcontrol`; Q preserved |
| `stdin-pty` | 57–67 | Real canonical POSIX terminal input `Q\n`; Q preserved |
| `capture-exact`, `capture-over`, `capture-large` | 69–74 | Exact 65,536-byte stdout/stderr prefix; truncation at 65,537/1,048,576 only |
| `arguments` | 75–77 | Exact UTF-8/NUL argument roundtrip, leading/trailing empty argument |
| `missing`, `expired`, `stop` | 78–80 | Start-error, expired-deadline and stopped statuses |
| `timeout-pause`, `timeout-continuous`, `timeout-budget`, `timeout-tree` | 81–85 | Timeout status, <6,000ms, continuous output on both streams |
| `crash`, `tree-exit`, `tree-fail`, `tree-crash` | 86–94 | Exact normal exit where specified; crash otherwise; result survives |
| `owner-isolation` | 139–160 | Two independent three-PID trees survive separate successful/failing/crashing/timed probes; all probe PIDs disappear; killed owner tree disappears and reaches EOF while unrelated tree remains alive |
| `owner-group-term` | 163–174 | SIGTERM to owner's independent POSIX session group preserves cleanup and EOF |
| `parent-death` | 177–187 | Stable parent reference: `WATCH_READY`, owner termination, exact `PARENT_GONE\n`, actual EOF |
| `launch-death-00` … `launch-death-19` | 190–202 | Twenty independent launch-window owner deaths; bounded actual pipe closure and disappearance of all observed numeric PIDs |
| `compiler-probe` | 204–205 | Probe deadlines without limiting user compilation |
| `invalid-timeout-{zero,negative,decimal,empty,plus,overflow,huge}` | 207–212 | Invalid timeout rejected before missing input open; nonzero status and timeout diagnostic |

Line references identify the preserved 213-line original revision. The semantic
names, not line offsets, are stable.
The common original `run()` checks—normal launcher exit, `cleanup == 0`, and
`error == 0` except for `missing`—remain enforced for every applicable case.

## Independent byte fixtures

These are original literals/formulas, not captured production outputs:

* `stdin-q.bin`: `b"Q"`.
* `stdin-file.bin`: `b"Qcontrol"`.
* `stdin-terminal.bin`: `b"Q\n"` (canonical terminal input).
* `capture-prefix.bin`: `bytes(range(256)) * 256`.
* `arguments.bin`: UTF-8 NUL-separated `""`, `"a b"`, `a"b`, `tail\`,
  `&%x%!`, `Привет😀`, `""`; leading/trailing NUL preserved.

All five files are binary (`-text`), with exact lengths and hashes recorded in
`provenance.json` and `SHA256SUMS`. Verify with:

```sh
(cd test/data/child-process && sha256sum -c SHA256SUMS)
```

## Ownership, observation and platform boundaries

The test-only raw launcher uses OS pipes/file handles, optional POSIX PTY and
`setsid`, and OS process creation directly. Readiness lines precede identity
pinning. Only those pinned identities may be signalled during deferred failure
cleanup; numeric PIDs reported after a completed probe are observation-only.
Owner reaping and descendant-held pipe EOF are separate bounded observations.
Readiness uses POSIX `poll` on the actual output descriptors, avoiding an
artificial launch-window delay. Windows anonymous synchronous pipes use a
nonblocking `PeekNamedPipe` pump with at most a one-millisecond wait between
checks (owner-handle wait while live); actual EOF, not elapsed time, proves pipe
closure. The generic ten-millisecond predicate cadence is not used for lines.

POSIX disappearance preserves the original `kill(pid, 0)` semantics: a zombie
is **still alive** until reaped by the tested cleanup service. A pidfd exit event
alone is not sufficient. The observer neither adopts nor reaps descendants.
Windows uses a synchronization handle and zero-time wait. Unexpected native
observation errors fail closed. POSIX-only PTY and process-group cases are
explicit skips on Windows, not silent substitutes.

## Validation limits

Unchanged original Debug and Release runs both reached `tree` and failed its
`cleanup == 0` assertion with native ENOENT (`cleanup=2`); this executor lacks
`/proc/thread-self/children`, needed by the production Linux cleanup service.
All preceding original assertions passed, including held-open pipe, regular
file, PTY, full binary capture and Unicode argument checks. The failed original
run is not reported as full parity, and its later owner/lifetime assertions were
not reached. No cleanup assertion or process-gone check is weakened. Strict
original and C cross-platform CI remains the full-parity gate.

The unchanged original tail blocks were additionally replayed independently
(with the same original assertions, not replacement expected values). In both
Debug and Release, owner isolation failed on `tree-exit` cleanup=2, group SIGTERM
failed the six-second PID-disappearance assertion, and parent-death passed.
The Debug launch-window block completed all 20 iterations; Release encountered
an eight-second EOF timeout after `LAUNCHING`, three numeric PIDs and `READY`,
the same observable failure shape as the C runner. This is a scheduling-sensitive
race: Debug success does not establish full parity or justify ignoring a C EOF
failure. Replay outcomes were recorded before supplemental emergency cleanup. They do
not establish cleanup correctness. Strict CI must validate the complete original
and C suites in an environment supporting cleanup adoption.

After replacing the generic polling interval with pipe-readiness notification,
all 20 C launch-window scenarios passed in both Debug and Release with their
original eight-second EOF and six-second disappearance limits unchanged.
Retained C artifacts additionally matched 18 original invocations in each
configuration: every JSON field and stderr byte matched, except variable elapsed
milliseconds and the pause fixture's numeric PID. Raw values remain in artifacts;
timing bounds and numeric PID syntax were checked, not silently discarded.
Together with the 29 non-cleanup scenarios, this gives 49/55 local passes per
configuration. The remaining six cleanup-sensitive cases still require strict
remote validation; they are not skipped or counted as passes.

Final independent review exercised 88 controls: 70 synthetic oracle controls
(22 positive and 48 deliberately mutated), four real OS zombie controls, six
watchdog/held-open-input controls, six final line-event controls and two late-PID
noninterference controls. The latter keep a driver-owned sentinel alive while the
runner correctly rejects its late reported PID after the six-second bound; only
the driver then terminates/reaps that pinned sentinel. All expected
outcomes passed; these controls verify the assertions and external watchdog,
not the unavailable production cleanup behavior. The 29 non-cleanup scenarios
also passed under UBSan. Original compiler-probe and all seven invalid-timeout
blocks were replayed unchanged and passed separately in Debug and Release.

Cleanup is finite but cannot safely terminate unidentified descendants reported
after their owner has died: late numeric PID lines are not live identity
handshakes, even if followed by a buffered READY. They remain observation-only;
the harness must not reopen and signal those PIDs or adopt descendants to hide
a failed cleanup assertion. Avoid repeated known-failing local reproductions;
strict remote validation is the acceptance gate. Deferred cleanup retains the
existing 40-second pinned-process and possible 30-second direct-owner reap
margins. The outer CTest limit is 300 seconds for owner isolation (two owners),
150 seconds otherwise; the internal 120-second owner-isolation scenario deadline
and all original per-operation bounds remain unchanged.

## Shared transport regression checks

The unchanged module-loading scenarios passed 94/94 in both Debug and Release.
The inspect-probe set passed 46/48 Debug and 45/48 Release locally: tree and
tree-crash retain their documented cleanup limitation; Release additionally
observed an extraction directory remaining after owner termination. The latter
was not a readiness/transport failure. Relinking the identical Release probe
and fixture objects against the original master transport and the new transport
then gave 3/3 passes each in alternating isolated runs. This does not establish
the cause of the initial cleanup failure; full shared-suite CI remains required.
