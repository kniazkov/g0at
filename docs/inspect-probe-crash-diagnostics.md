# Strict crash-fixture diagnostics (#509)

The inspection fixture tests real abnormal termination and cleanup, not the
host's core-dump reporter. On Linux its crash path now sets and verifies
`PR_SET_DUMPABLE=0` immediately before the crash marker and `abort()`. A failed
setup exits normally with code 99 and cannot masquerade as the required SIGABRT.
This changes only the disposable test worker, not system settings or production
process handling. Windows keeps its existing WER-avoiding termination path.

All four crash scenarios still require `crashed` with the expected signal/code.
The cleanup-timeout scenario keeps its one-second probe deadline and its separate
cleanup expectations. Linux assertions additionally require the core-disabled
marker immediately before the crash marker. No timeout is accepted as a crash;
production deadline/cleanup semantics are unchanged.

## Reproducible stress run

Manually run the **Inspect crash diagnostics** workflow and provide an exact
baseline commit. Its four jobs compare baseline/patched fixtures in Debug and
Release. Both the fixture source and scenario assertions are restored for the
baseline, so it does not fail merely because the new marker was not present.
The driver verifies the exact four registered test names before running:

```
cmake -DBUILD_DIR=build/probe -P scripts/check_inspect_crash_stress.cmake
```

It uses four workers and `ctest --repeat until-fail:40`, retains failure status,
and uploads host core policy, process snapshots, test logs and retained scenario
work. It does not change `core_pattern`, limits or any system service. It is not
part of ordinary PR CI, which continues to run the normal crash scenarios.

## Evidence and remaining limits

Focused run [43196115136](https://github.com/kniazkov/g0at/actions/runs/43196115136)
compared baseline `00cf3b7df9fb57c03c34b8be30c4e1bc96c56a5f` with the two-file
fixture/assertion patch. All four jobs passed: 320 baseline and 320 patched test
executions, including the descendant-tree scenario. The host used a piped
`systemd-coredump` core pattern even though the shell's core size limit was zero.
The one-second snapshots observed coredump helper processes in both baseline
jobs and none in either patched job. Snapshot absence alone is not a proof of
absence; the checked dumpability policy is the direct control.

Local positive/negative controls confirmed real SIGABRT with dumpability zero,
rejection of the old fixture by the new assertion, and normal exit 99 when the
policy setup was denied. Three scenarios each passed 40 repeats in Debug and
Release locally (240 executions). The local descendant-tree cleanup check failed
on both the patch and exact baseline in the restricted cloud environment; those
local full stress runs were reported as failures, not green. Native CI passed
that scenario in all four jobs.

The historical #509 failure was NOT reproduced by this stress run: the baseline
also passed. Its retained original report shows the first invocation crashed
with SIGABRT and expected cleanup timeout, while the second invocation timed out
in load. It does not timestamp signal delivery, core processing or scheduling.
This patch removes a verified, unwanted host-reporter dependency from the
fixture; it is not proof that the reporter caused that particular historical
failure. Keep that distinction when deciding whether #509 can be closed.
