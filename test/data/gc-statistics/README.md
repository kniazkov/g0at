# GC-09 scoped telemetry workload

This uses the existing explicit model collector in the test host. It does not
add a program-level call or change the VM's collection policy.

```sh
cmake -S . -B build-stats -DCMAKE_BUILD_TYPE=Release
cmake --build build-stats --target unit_testing
ctest --test-dir build-stats -R '^core\.gc_stats$' --output-on-failure
./build-stats/unit_testing --gc-stats-bench
```

Nine collections of a 20,000-link deep cycle plus a 4,096-slot wide root
exercise a large live heap with shared references. The workload first measures
the maximum pass with diagnostics disabled, then the maximum instrumented
pass on the same graph. Removing the outside root lets one explicit pass
reclaim the cycle. Another phase times 100,000 ordinary short-lived
array/integer pairs (reference counting and reuse, no tracing), and explicitly
trims the pools. The test asserts deterministic counts and ownership behavior
for small graphs and independent processes, not timing. Time values are
host observations, not a portable service-level or hardware guarantee.

## Example measurement

Five fresh executions on the development Linux x86-64 GCC Release build
reported 20,004 marked vertices, 44,100 edge visits and 1,953,304 active payload
bytes in each live phase. Once no longer rooted, the cycle phase began with
20,002 active objects, left the one baseline context object and removed
1,920,096 tracked heap bytes. The refcount-only phase's explicit trim freed
48 pooled bytes and left the tracing pass count unchanged at ten.

| Phase | Range across five host runs |
|---|---:|
| Maximum live pause in each nine-pass run, diagnostics off | 0.422–0.917 ms |
| Maximum live pause in each nine-pass run, diagnostics on | 0.610–0.922 ms |
| Instrumented cycle reclamation | 0.566–1.319 ms |
| 100,000 ordinary allocate/release pairs with pool reuse | 5.064–6.470 ms |

These include cold scheduling and cache variation; the stats-on time includes
two optional byte snapshots. The intervals do not set budgets or determine
whether a later collector needs a different strategy.
