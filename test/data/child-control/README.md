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
