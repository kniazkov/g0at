# Automatic GC sustained workload (#642)

Build and run (assertions are enabled for this target in Release too):

```sh
cmake -S . -B build-stress -DCMAKE_BUILD_TYPE=Release
cmake --build build-stress --target gc_policy_testing
ctest --test-dir build-stress -L gc-stress -V
```

The three tests are part of `gc-acceptance`, already run explicitly by Linux
GCC/Clang and Windows Debug/Release CI. Each has a 180-second timeout.
`ctest -V` records all phase measurements; failures retain preceding flushed
measurements. No random generator or seed is used: the topology and iteration
counts are fixed in `test/src/component/gc_policy_testing.c`.

Each worker performs 20,000 iterations, producing three dead cycles per
iteration, one pointing to a live neighbour. Every iteration exercises
throw/catch/finally and a string bridge; spawn/join surrounds the worker.
There are 20 observation phases, one per 1,000 iterations. The large live set
holds a chain of depth 1,024 and a fanout of 1,024 arrays. After allocation
pressure, every retained element and chain link is traversed and checked.

* `stress-compare`: small and large live sets, each run twice in the same process.
  It requires fewer automatic collections with the large adaptive threshold.
* `stress-native`: serialized in-memory module with a scalar-v1 adapter,
  20,000 calls with a borrowed heap string; genuine pending requests only.
* `stress-pressure`: the same module/native workload plus one host pressure
  request per phase; each must be serviced exactly once, including the last
  request at run end. No forced instruction-boundary GC is enabled.

In total: 120,000 iterations, 360,000 dead cycles and roughly three million
allocation events. This tests sustained allocation, not hours of wall time.
Existing boundary/concurrency tests remain responsible for scheduling races.
The native fixture tests ABI/lifetime interaction, not external compiler speed.

## Budgets and interpretation

At every checkpoint require GC progress, fewer than 8,192 active objects,
active payload below 256 KiB (small) or 512 KiB (large), auxiliary payload below
64 KiB, pools within the production `GOAT_POOL_BYTE_LIMIT`, and fewer than 64
external roots. The measured large live set is about 212 KiB; 512 KiB allows
one budget of unreclaimed garbage and representation differences. Bounds stay
constant across all phases, so an accumulating heap eventually fails. More
than 16 allocation events per automatic collection and fewer than 10,000
collections per run reject collection-per-allocation regressions. The relative
small/large comparison detects losing live-set adaptation.

After every run: active payload below 64 KiB, bounded pools/auxiliary storage,
no active workers, roots, session, execution or allocation debt. Small retained
contexts/caches explain the nonzero payload between runs. After full destruction,
the allocator payload must exactly equal its pre-test value.

`active`/`pool`/`aux` are Goat payload accounting, not allocator headers,
fragmentation, code/metadata, thread stacks or RSS. `live` is the policy's last
post-collection live measurement. `pause_max_ns` is the maximum completed GC
pause in the phase (the observer resets only that maximum). Pauses are reported,
not compared with a universal hardware-dependent deadline. Pressure does not
promise lower GC counts or return of memory to the OS.

## Recorded Linux x86-64 Debug sample

GCC 14.2.0, default CMake Debug, 25 November 2026, based on master `1a489165`.
Raw per-phase and teardown results are the adjacent `linux-debug-*.txt` files.
They are evidence, not golden output; scheduling and timings may vary.

| Workload | Automatic collections | Peak sampled active bytes | Maximum observed phase pause |
|---|---:|---:|---:|
| VM small + large, two runs each | 2,883 + 594 | 242,892 | 3.887 ms |
| Module/native large | 252 | 243,680 | 0.481 ms |
| Module/native large + pressure | 244 | 231,080 | 3.346 ms |

After run, active payload is 520/616 B for the repeated VM and 2,268 B for
modules, pool payload at most 5,088 B, auxiliary payload 192 B. All workload
budgets pass. These measurements do not establish an application latency SLA;
no new correctness or budget blocker was observed in this bounded workload.
