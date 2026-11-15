# Completed worker memory fixture (#549)

Build the ordinary project, then run the following from the checkout:

```sh
ctest --test-dir build -R '^core\.thread_memory\.' -V
ctest --test-dir build -R '^integration\.module_threads\.lifetime-native-' --output-on-failure
```

Both stress modes start with two truly blocked waiters on a shared object-valued
target. They then run 512 sequential program workers with an acyclic array
of 65,536 element slots held by the working context and stack, and a small
integer result. One mode joins each worker twice; the other waits at the host
observation probe without using language join until a late join of the first
worker. Both verify repeated exception identity and its original trace.
The third fixture checks an unobserved failure reaching the entry diagnostic.
The two existing lifetime tests exercise normal entry completion, a child
creating a descendant, late first native dispatch, and exactly-once cleanup.

Sample from the development Linux x86-64 Debug run (the first and last sampled
values are **before** VM shutdown):

| Runtime | Mode | Goat-tracked live bytes, first → last | Process current RSS, first → last | Retained worker records | Pending OS handles, peak / last |
|---|---|---:|---:|---:|---:|
| Baseline `071cc9d0` | Repeated join | 548,496 → 269,620,752 | 3,936 → 272,932 KiB | 515 × 176 B | 515 / 515 |
| GC-07 | Repeated join | 17,582 → 128,150 | 3,380 → 4,548 KiB | 515 × 216 B | 0 / 0 |
| Baseline `071cc9d0` | No join inside loop | 548,071 → 269,595,799 | 3,868 → 272,728 KiB | 515 × 176 B | 515 / 515 |
| GC-07 | No join inside loop | 17,197 → 127,685 | 3,260 → 4,412 KiB | 515 × 216 B | 1 / 1 |

The baseline used unchanged production sources with the same fixture workload
and a probe adapted only to read its outcomes from the worker's stack and
record, without the new memory and pending-handle assertions.

The transient worker raised Goat-tracked bytes by up to 524,544 B during its
callback. The measured long-run increase is mostly the records required for
late/repeated joins; unlike a working stack, their results can be genuinely
live. The first sample already has four such records. Both modes require zero
remaining OS worker handles after the run and balanced Goat-tracked allocations
at process teardown. The tests assert those invariants and a broad tracked-byte
budget, not RSS: RSS includes OS stacks, allocator caching, and other memory
outside Goat's ledger, and can vary across runs. Only Linux reads current RSS
from `/proc/self/status`; other platforms print 0 for that unavailable field.
This is not a Raspberry Pi measurement or an estimate for another platform.
