# Pool retention and reuse workload (#550)

Use the ordinary build plus allocation instrumentation for counted workloads:

```sh
cmake -S . -B build-pools -DCMAKE_BUILD_TYPE=Release -DGOAT_TEST_ALLOC_FAILURE=ON
cmake --build build-pools --target unit_testing
ctest --test-dir build-pools -R '^core\.gc_pools$' --output-on-failure
./build-pools/unit_testing --gc-pool-bench
```

The large workload holds 24 user objects, each with 1,024 fields and 96
distinct borrowed-at-call, retained-as-prototype objects. It measures Goat
allocator payload above the process starting state, while live and after
releasing them. Candidate trim then releases all accounted pool bytes; the base
has no trim operation. The small workload separately creates a fresh process,
warms the pools, then repeatedly creates and releases one user object with a
small key/value graph (100,000 iterations). It reports allocation attempt count
and CPU time. Unit assertions cover the payload bounds, exact ledger delta,
unrelated live-object survival and allocation-free trim; timing and RSS are
not assertions.

## Linux x86-64 Release sample

Seven alternating runs used the same GCC build configuration and
`GOAT_TEST_ALLOC_FAILURE=ON`. The base was exactly `c04640059baed60ba56e59d0a617b161f2f7eeae`:
only the benchmark probe was added there, with an absent trim reported as 0;
its production sources were unchanged.

| Metric | Base | GC-08 |
|---|---:|---:|
| Large live payload above process start | 4,208,832 B | 4,208,832 B |
| Payload retained after dropping large objects | 520,512 B | 84,288 B |
| Explicit trim released | Not available | 84,288 B |
| Small workload allocation attempts | 300,001 | 300,001 |
| Small workload median CPU time, 100,000 iterations | 10.711 ms | 10.903 ms |
| Small workload CPU time range | 9.511–11.228 ms | 10.601–23.923 ms |

The 64-slot cut preserves small warm vector allocations while discarding large
backings on return. The 512 KiB aggregate cap is an initial per-process
ceiling in addition to independent shell-count limits, not a measured optimum
or a target-device guarantee. The warmed benchmark found no additional
allocations and similar medians, with noisy timing on this host.
