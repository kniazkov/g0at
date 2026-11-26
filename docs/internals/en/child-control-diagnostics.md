# Linux supervisor reply failures (#645)

A child-process result and its supervisor's status are different facts. The
supervisor first cleans its process tree, then sends one `SOCK_SEQPACKET` reply.
Previously both reply send sites ignored a possible `EINTR`; the owner mapped
EOF, receive errors and cleanup deadline expiration to the same `cleanup=EIO`.
A short packet was also not classified as a protocol error.

Reply sending now repeats **only** an interrupted send syscall. It does not
restart a compiler, rerun a child, repeat cleanup or forgive a terminal send error.
A terminal failure still makes the owner fail closed. The successful reply path
and existing tree-cleanup status are preserved.

`child_result_t` now carries Linux transport diagnostics:

| Field | Meaning |
|---|---|
| `control_error` | 0 without transport failure; `EPIPE` for EOF without reply, `EPROTO` for a short packet, actual recv errno for a receive failure, `ETIMEDOUT` when the cleanup reply deadline expires |
| `supervisor_reaped` | Whether waitpid actually collected a supervisor status |
| `supervisor_status` | Raw Linux wait status; valid only when `supervisor_reaped` is true, decoded using WIFEXITED/WEXITSTATUS or WIFSIGNALED/WTERMSIG |

These fields are zero on other platforms. `system_error` continues to describe
launch/system errors; `signal_number` describes the child, not the supervisor.
`cleanup_error=EIO` remains nonzero when no complete reply proves cleanup. The
native toolchain includes all three new fields in its existing bounded diagnostic.
The internal cleanup-service wire message uses the same rebuilt structure on both
ends; this is not a change to Goat module metadata or the native scalar ABI.

On a transport failure, status collection is nonblocking: the supervisor may
still be alive or exiting, or an embedding SIGCHLD handler may have reaped it.
`supervisor_reaped=0` means unavailable evidence, not successful exit. The failure
path makes no new promise that an unexpectedly killed supervisor cleaned its tree.
No environment or executable arguments are dumped by this diagnostic.

## Permanent tests and evidence

`integration.child_control.*` links a separately compiled Linux implementation
with compile-time hooks; production builds have no environment switch or public
fault-injection API. Faults are inserted only at final reply delivery, after real
child-tree cleanup. The modes are `ok`, `eintr`, `eof`, `signal`, `short`,
`send-error`, `receive-error` and `launch-error`. They check fail-closed results,
distinct transport errors, available supervisor status, stable repeated wait and
allocator balance. The child is `/bin/true`; launch-error uses an absent path.

```sh
cmake --build build --target child_process_scenarios
ctest --test-dir build -L child-control --no-tests=error --output-on-failure
```

Ordinary Linux Debug/Release integration runs include these tests. The dedicated
native dependency workflow also runs each fault 25 times, then performs 50 real
StringBuilder/native runs per configuration (5 isolated, 15 with concurrency 3,
30 with concurrency 6). Its 12 configurations span Ubuntu 22.04/24.04,
GCC/Clang, Debug/Release/ASan+UBSan. Every failure remains a failure; repeat counts
measure reproducibility and do not turn a failed run into success.

The negative control removes the EINTR loop and deterministically fails the
`eintr` success assertion. Restoring the loop fixes that injected failure.
This proves a transport bug, **not** the cause of the historical CI incident:
its old `status=6 system=0 cleanup=5` did not preserve enough information to
identify EOF versus errno or supervisor death. The earlier generic ELF error is
also not retroactively diagnosed. #645 stays open pending the diagnostic matrix
and review of its evidence; successful reruns alone do not prove the original
intermittent failure fixed. See the adjacent recorded workload report under
`test/data/child-control/README.md`.

## Cleanup-service transport regression

The `service-*` variants repeat all eight faults through a real separate
cleanup-service process, as used by compiler probes. They require `CLEANUP_OK`
for successful delivery of the child result even when that result reports
`CHILD_IO_ERROR`; final cleanup must report `CLEANUP_TREE_FAILED` in that case.
Launch failure is distinct and does not claim tree cleanup failure. Repeated
finish calls must preserve the recorded outcome. The test-only environment
selects a fault in the fixture executable, never in production binaries, and
leaves the service-launching supervisor healthy. This guards against losing
transport diagnostics while serializing the internal child-result structure.

PR #658's investigation passed all 12 configurations (600 native workloads).
That did not reproduce the historical ELF incident. These service tests cover
a previously untested route; they do not assign the old failure a proven cause.

Local repeated ASan service testing exposed a startup failure before fault injection.
The original failure is retained in `test/data/child-control/linux-asan-service-first-failure.txt`.
A subsequent 400-run diagnostic pass succeeded; it does not explain the failure.
Startup status and elapsed time are now printed. Follow-up blocker: [#659](https://github.com/kniazkov/g0at/issues/659).
