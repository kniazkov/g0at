# GC boundary and embedding audit (#635)

This is the current integration contract after #633/#634. Earlier chronological
sections in memory-management.md describe implementation stages, not additional
permissions. Automatic allocation thresholds remain a separate task (#638/#639).

## Admission and ownership

`configure_gc_boundaries` is an idle, model-locked, exclusive-host opt-in to forced
runtime checks. It does not authorize arbitrary callbacks. A normal instruction
owns one executor exclusion. Reviewed initializer frames account for their extra
lock depth explicitly. `gc_call_wait_begin` requires the same thread/session,
exactly one exclusion, no publication, matched initialization accounting and lock
depth `1 + gc_call_extra_lock_depth`. A failure to qualify leaves the exclusion
intact. Publication is withdrawn before manipulating managed state again.

`process_execution_suspend/resume` only releases/restores recursive mutex depth.
It never grants GC permission. INCREF alone does not publish a C local to tracing.
An owner that permits collection must register every otherwise invisible managed
value as an owning root; borrowed cells, topology, strings and queues cannot outlive
a lock release unless their owners and mutation contract have been reviewed.

## Inventory

Paths are relative to the repository root. Test families name CTest registrations.
Every row either permits a narrowly reviewed boundary or deliberately defers GC.

| Path / boundary | Live ownership and lock/publication decision | Executable coverage |
|---|---|---|
| `src/model/process.c`: enter/leave/suspend/resume | Mutex mechanics only; no inferred publication, full recursive depth restored | `core.gc_protocol`, `core.gc_embedding` |
| `src/vm/vm.c`: instruction dispatcher | Stack/context roots; executor blocks collection; completed instruction may publish at exact depth | `core.gc_runtime`, `core.gc_dispatch` |
| `vm.c`: CALL/CALLM and operator dispatch | Callable/receiver and popped builtin args have owning roots; arbitrary builtin body stays excluded; only exact reviewed builtins/dynamic execution get scoped admission | `core.gc_module_initialization.origin-*`, `operator-*`, `core.gc_embedding` |
| `vm.c`: property, bound access and origin preflight | Stack/context roots; observe waits before borrowing cells, restart lookup after resumption | `core.gc_module_initialization.property-*`, `bound-*`, `origin-*` |
| `src/model/module_files.c`: publication wait and initializer gate acquisition | Registry owns scopes; reviewed observer counts waiters; release model lock before gate acquisition; unknown callers remain excluded | `core.gc_module_initialization.*`, including recursive refusal |
| `vm.c`: execute_module_file and reviewed initializer frames | Saved exception rooted, frame/scope and caller state retained; extra lock/executor depth accounted, restored on error | `core.gc_runtime`, `core.gc_module_initialization.*` |
| `src/model/function.c`: scalar-v2 invoke | VM stack owns borrowed arguments; native allocations are unmanaged; scoped wait, resume before converting results | `core.gc_runtime_native.*`, `core.gc_native_wait.*`, `core.gc_native_startup.*` |
| `src/model/module_native.c`: scalar-v1 invoke | VM argument roots and stable module binding; same wait/depth rule; errors never replay entered native code | `core.gc_runtime` module adapter cases |
| Both scalar host-exception services | Enter model lock, pause publication while mutating exception, resume before releasing added lock; no arbitrary host reentry permission | scalar host-exception variants in `core.gc_native_wait.*` and `core.gc_runtime` |
| `module_native.c`: resolver callback | Default veto; only idle explicitly reviewed binding can publish; custom resolver retains exclusion | resolver cases in `core.gc_runtime` |
| `src/vm/module_loading.c`: loader mutex/I/O and report/write hooks | Snapshot/library state stable for run; suspend model before loader mutex; custom writer/report veto reviewed resolution | loading/native resolver suites; `core.gc_runtime` |
| `vm.c`: native continuation driver and step | Only completed steps publish; operands on VM stack, no model values held by driver; exceptions/cancellation do not replay | `core.gc_continuation.*` |
| `src/builtins/{print,println,input}.c` | Receiver/arguments rooted; output snapshot unmanaged; publish only during blocking I/O, withdraw before model conversion | `core.gc_io.*` |
| Observation and copy preflights (`gc_protocol.c`, `user_defined_object.c`) | Rooted source, restart borrowed traversal after waits; render/materialize under lock with publication ended | `observation-*`, `copying-*` in `core.gc_module_initialization.*` |
| `vm.c`: join and terminal condition waits | Thread registry retains result/exception; reviewed join argument rooted; reject initializer join; terminal publication only at depth one | `core.gc_runtime` wait/startup/terminal cases |
| `vm.c`: schedule_and_reap | Finished records remain registered while detached OS handles are joined outside model lock; pending reaping vetoes collection; caller controls publication | `core.gc_runtime`, `core.gc_dispatch.*` family |
| Public `run` / `run_module_file` reentry into active process | Reject before modifying execution/session/context/stack; independent process may run but cannot publish the suspended outer C frame | `core.gc_embedding` |
| Unreviewed C callbacks and extra host locks | Explicit veto, even after suspend/resume or recursive execution; pending request retained until a later safe boundary | `core.gc_embedding`, `core.gc_protocol`, recursive runtime/native cases |

The inventory includes all production calls of `process_execution_suspend` and the
execution-mutex condition waits. Plain execution-mutex leave at API return and
worker completion ends ownership; it is not an independent safepoint. Other
compiler/cleanup-service OS waits own unmanaged state and do not independently
publish a Goat runtime participant.

## Progress and embedding limits

An arbitrary callback may delay GC indefinitely, including while blocked in host
I/O. Holding additional host locks or reentering an excluded C frame does not make
its locals traceable. This is an explicit supported refusal, not a bounded-pause
claim. A host must return to a reviewed boundary to permit collection; it must not
force `collect_garbage` or forge participant fields from inside that callback.

Requests coalesce and remain pending on refusal. The new six-case embedding matrix
checks zero/one/two extra recursive locks, normal/exceptional callback exit,
rejected standalone/module reentry, nested execution of an independent process,
a cyclic object owned only by a C local, and collection after the excluded frame
returns. It checks unchanged caller state, bytecode and allocator/root balance.
An independent nested process must not invoke cross-process host lock cycles;
this test deliberately performs only same-thread recursive ownership.

## #617 acceptance mapping and remaining work

- Standalone/module VM and nested functions: `core.gc_runtime`; controlled nested
  executor deferral: `core.gc_dispatch`; saved pending state: continuation and
  initializer suites. Finally paths are in runtime tests; constructor snapshots
  retain existing `component.init_snapshot.*` coverage.
- Both scalar ABIs, borrowed strings, host exception and native errors: runtime
  adapter, scalar-native and native-wait families. Continuation errors/cancellation:
  `core.gc_continuation.*`. Entered native bodies never fall back to replay.
- Workers, bootstrap, join, terminal waits and reaping: runtime, dispatch, I/O and
  startup families. Module/property/operator/copy waits: initializer family.
- Arbitrary callbacks, recursive host frames and retained deferred requests:
  protocol/runtime refusal cases plus `core.gc_embedding`.

No additional production admission is justified by this audit. #617's child
implementation checklist can be reviewed after #635 merges; #637 still owns the
combined acceptance campaign. #638/#639 own automatic policy, #640 pressure,
#641 fatal OOM and #642/#643 stress/final CI. No bounded progress for an arbitrary
nonreturning host callback is promised or silently assumed by those stages.
