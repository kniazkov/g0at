# Test support

Shared test framework components will be added here during the migration.

The data relocation maps `test/{analysis,binary-v1,functional,native,performance,unicode}`
to the same names under `test/data/`. Native Python checks moved from
`test/native/checks/` to `test/src/native/checks/`; native C hosts already live
under `test/src/native/`. Format specifications and their vectors remain in
`docs/module-format-v1/`.

## Execution and diagnostics

`execution.h` adds a test-only fail-fast scope. `scenario_execute` requires a
nonzero overall timeout; the same absolute monotonic deadline is used for every
command, never restarted. Required checks abort the body and invoke registered
resource cleanup callbacks in reverse order. Register ownership immediately;
callback resources must outlive the body stack (for example caller-owned state or
heap allocations), because failure unwinds the body with `longjmp`. Checks remain
active in Release builds.

`scenario_command` saves length-delimited command arguments and exit/status details
in `execution.log`, and complete binary output in `command-N.stdout` and
`command-N.stderr`. `scenario_require_bytes` saves raw expected/actual spans,
including NUL, spaces and empty lines. Its explicit text option normalizes only
CRLF pairs, never whitespace or lone CR. Diagnostic write/close failures are test
failures. Failed and `--keep-work` workspaces are retained by the existing context.

Use `scenario_supervise` for a separate test host that can crash or hang: the
parent owns the deadline, diagnostics and process tree even if the host cannot
return. In-process `scenario_execute` bodies are cooperative and must reach API
checkpoints; arbitrary C loops and cleanup callbacks cannot be preempted safely
in-process. Callbacks must return promptly. This is lifecycle isolation, not a
security sandbox. CTest's outer timeout must exceed the internal scenario budget
by a cleanup margin; framework self-tests allow 30 seconds for budgets up to
5 seconds. Independent CTest cases continue after a failure by default.

Run `ctest --test-dir build -L scenario-execution --output-on-failure` for framework
self-tests. Each deliberate failure is observed by an independent outer test,
which checks the failure result, retained artifacts and skipped subsequent steps.
No existing Python test is removed by this framework addition.

## Independent integer arithmetic (#334)

`integer_oracle.h` / `integer_oracle.c` provide exact sign/magnitude results in
four little-endian 32-bit limbs, then explicit int64 saturation. The test-only
`goat_integer_oracle` target has no production dependency and is never linked
into the interpreter. There is no `__int128` requirement or signed overflow.
See `test/data/integer-oracle/README.md` for Python provenance, reproduction,
scenario mapping and deliberate-corruption self-tests. The existing
`test/src/native/checks/saturation.py` consumer is unchanged until #386;
this step supplies its future independent C oracle, not a replacement runner.

Run `ctest --test-dir build -L integer-oracle --output-on-failure` after building
`integer_oracle_testing` (and `integer_oracle_ubsan_testing` on Unix GCC/Clang).
The same label runs in Debug/Release on Linux x86-64/AArch64 and Windows
MinGW i686/x86-64/UCRT. UBSan instruments the oracle and caller in the glibc Linux CI jobs;
the target is opt-in and excluded from default/package builds because Alpine
musl does not ship the UBSan runtime in its build toolchain.
Windows uses the same portable uninstrumented tests, since MinGW's configured
CI toolchains do not provide the Unix UBSan runtime.

## Independent JSON format oracle (#335)

`format_json.h` / `format_json.c` implement strict parsing, canonical bytes and
schema-subset validation using the shared declarative
`docs/module-format-v1/schema-source.json`, not generated production tables.
`format_analysis.h` / `format_analysis.c` implement scalar/facts normalization and
summary consistency. `format_metadata.h` / `format_metadata.c` implement metadata,
native guards, hash projection and the length-delimited main-hash input.
These files form the test-only `goat_format_oracle` target. Its sole parser/DOM
substrate is yyjson; it has no dependency on `goat_module`, `goat_validation` or
`core`. No production canonicalizer or validator computes expected results.

The new runner `format_oracle_testing` selects individual scenarios through
`ctest --test-dir build -L format-oracle --output-on-failure`. It only reads frozen
inputs and schemas; normal tests never regenerate vectors or source files.
The existing Python reference remains unchanged for the binary migration (#336)
and later consumers. `docs/module-format-v1/test_reference.py` still runs in CI.
The old-to-new scenario map and corpus provenance are in
`test/data/format-oracle/README.md`.

Remaining Python import consumers (including dynamic imports) are:
`docs/module-format-v1/test_reference.py`,
`check_module_canonical.py`,
`check_module_container.py`, `check_module_delivery.py`,
`check_module_hash.py`,
`check_module_musl.py` and `check_module_search.py`. They retain their current
API and data paths; their migration remains in the ordered checklist.

## Independent binary and fixture oracle (#336)

`format_binary.h` / `.c` add independent bytecode construction/validation,
JSON/binary consistency and SHA-256. `format_zip.h` / `.c` construct ZIP records
without production writers, preserving caller order and duplicate entries, and
expose inspection, extraction and bounded byte patching for negative fixtures.
`format_fixtures.h` / `.c` append real external declaration blocks without moving
existing instruction indices. These are part of test-only `goat_format_oracle`;
its extra dependency is the bundled zlib primitive library, not Goat wrappers.
See `test/data/binary-oracle/README.md` for the old/new scenario map, provenance,
corpus hashes, remaining Python fixture consumers and reproduction commands.
The `format-binary` CTest label selects the new tests, also included in
`format-oracle` for the full existing cross-platform CI matrix.

## Shared execution and inspection helpers (#337)

`module_execution.h` / `.c` migrate `ExecutionSuite`, `Project` and `put` from
`scripts/module_execution_support.py` into the test-only support layer. They use
one scenario-owned work directory and deadline, preserve command/output
artifacts, compile real module descriptors, and execute verified binaries from
an unrelated working directory with a per-project `GOAT_MODULE_PATH`. The parent
environment and working directory are never changed. Output comparisons retain
explicit byte lengths and normalize only CRLF, not whitespace or trailing lines.

`inspect_report.h` / `.c` migrate the structural schema checks from
`scripts/inspect_report_test_support.py`. Published schemas and referenced JSON
schemas are read as data; reports are parsed structurally and duplicate object
keys are rejected, including escaped spellings. This is test policy, not a new
production inspector or a change to the JSON report contract.

The `migration-helpers` CTest label selects the new independently selectable
self-tests. Both runner targets are built by the existing shell/cmd entry points
and tested by Linux x86-64/AArch64 GCC/Clang and Windows MinGW i686/x86-64/UCRT
Debug/Release CI. Python helpers remain untouched until their last consumer is
migrated and #394 removes the transitional layer.

Remaining `module_execution_support` consumers:
`scripts/check_module_threads.py`,
`check_module_exit.py`,
and `check_module_delivery.py`.

Remaining `inspect_report_test_support` consumers:
`scripts/check_module_book.py`,
and `check_module_delivery.py`.
These keep their existing IDs and entry points; this step does not migrate their
scenario bodies or remove any Python test.

### Execution contract scenario map

| Original Python operation | New CTest ID |
| --- | --- |
| `ExecutionSuite.project`, `Project.compile/run`, `put`, files/defaults, real import | `helpers.execution.real` |
| Expected compiler failure and absent artifact | `helpers.execution.compile-failure` |
| Full raw stdout/stderr, NUL, CRLF and trailing lines | `helpers.execution.capture` |
| Inherited environment, module-path override, unrelated cwd and project isolation | `helpers.execution.environment`, `helpers.execution.real` |
| Exact stdout mismatch and actual-only CRLF normalization | `helpers.execution.mismatch`, `helpers.execution.expected-crlf` |
| Strict UTF-8 decoding of stdout | `helpers.execution.invalid-utf8` |
| Nonzero exit with required diagnostic; unexpected exit state | `helpers.execution.missing-diagnostic`, `helpers.execution.unexpected-success`, `helpers.execution.unexpected-failure` |
| Confine fixture writes to scenario work | `helpers.execution.invalid-path` |

The negative cases check that the scenario fails and never reaches the statement
after the rejected operation. A crashed/timed-out child is always an infrastructure
failure under the shared scenario contract, not an expected language/compiler
failure. Relative fixture paths reject traversal instead of writing outside the
scenario. These intentional safety constraints do not alter existing Python
consumers. `scenario_command_capture` extends the common process layer with
complete scenario-owned byte spans, preserving its shared deadline and artifacts.
The inspection scenario map and 96-case frozen Python parity reproduction live
in [`test/data/inspect-report/README.md`](../data/inspect-report/README.md).

Build and run:

```sh
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --target module_execution_testing inspect_report_testing
ctest --test-dir build/manual -L migration-helpers --output-on-failure --parallel 4
```

The same real-project inputs were checked with the unchanged Python helpers:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 - <<'PY'
import sys, tempfile
from pathlib import Path
sys.path.insert(0, 'scripts')
from module_execution_support import ExecutionSuite
with tempfile.TemporaryDirectory(dir='build') as root:
    suite = ExecutionSuite('build/manual/module_import_execution_testing', Path(root).resolve())
    p = suite.project('project with spaces')
    p.compile('library', 'external answer = (include "nested/other.goat").answer;',
              version='2', files={'nested/other.goat': 'public answer = 42;'}, defaults={})
    p.run('const lib = import "library"; println(lib.answer); println(" a\\0b " ); println("");',
          '42\n a\0b \n\n', defaults={'library': '2'})
    suite.project('isolated').run('println(7);', '7\n')
    p.compile('broken', 'const = ;', fail=True)
    assert suite.count == 6
PY
```

## Independent process observation (#338)

`process_watch.h` / `.c` replace the `ProcessWatch` and `until` contracts from
`scripts/process_test_support.py` for C consumers. The standalone
`goat_process_watch_support` library depends only on OS/C APIs, **not**
`goat_common`, `child_process`, or `cleanup_service`. Linux pins a pidfd and signals
through `pidfd_send_signal`; Windows pins a process handle and uses kernel wait
and terminate operations. There is no numeric-PID signalling fallback. Unsupported
Linux kernels return an explicit native error, not a passing test or silent SKIP.

Callers must establish readiness before opening a watch, and keep the process
waiting until its identity is pinned. A PID received without such synchronization
cannot prove identity before `process_watch_open`. Once pinned, death and PID reuse
cannot redirect a signal to another process. The observer does not launch or reap
processes; the launcher owns those responsibilities. `close` consumes and nulls
the pointer, is repeatable, and never kills an observed process.

Waits take an absolute monotonic millisecond deadline, shared across operations.
Exit is observed by pidfd readiness / process-handle signalling, not by sleeping.
The condition helper polls a caller-supplied bounded predicate like Python `until`;
only the predicate proves completion. Timeout and OS failure are distinct results.

The existing Python helper stays unchanged for these remaining consumers:

| Consumer | Scheduled migration |
| --- | --- |
| `scripts/check_cleanup_service.py` | #376 |
| `scripts/check_native_cleanup.py` | #381 |

Final removal remains #394. No existing consumer, scenario, sanitizer, fault test,
or musl test is removed in this step. The new C runner is added to the existing
Linux x86-64/AArch64 GCC/Clang and Windows MinGW i686/x86-64/UCRT Debug/Release
matrix. It uses direct OS launch and synchronization, independently of production
process/cleanup implementation, so a shared production bug cannot make it pass.

```sh
cmake -S . -B build/338-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/338-debug --target process_watch_testing
ctest --test-dir build/338-debug -L process-watch --output-on-failure --parallel 4
```

### Original operation → stable CTest ID

| Python helper contract | New CTest ID |
| --- | --- |
| `ProcessWatch.alive`, normal completion and `wait` | `process.watch.alive-wait` |
| `ProcessWatch.kill`, retained identity after reap | `process.watch.kill` |
| Bounded wait rejects a still-live process | `process.watch.deadline` |
| `ProcessWatch.close`, repeated close, native handle count | `process.watch.close` |
| Independently launched watcher observes owner death | `process.watch.owner-death` |
| `until` success, deadline and predicate error | `process.watch.condition` |

All process cases also reject closed/null watch operations where applicable,
check OS resources are restored, and retain a dead watch while launching another
process to verify signalling cannot target that new process. This does not force
OS PID reuse; the identity guarantee comes from the pinned kernel object, with no
PID-based signal path. Fixture cleanup can signal its own unreaped POSIX child
(which cannot yet reuse its PID); this is not the observer implementation.

### Python/C parity on identical live processes (Linux)

This temporary comparison loads the standalone C observer and unchanged Python
helper against **the same process identity** in each case: graceful exit, C kill,
and Python kill. Both reject a live-process deadline and agree on death before
and after reaping. No Python dependency is added to the new CTest cases.

```sh
mkdir -p build/338-parity
cc -std=c11 -shared -fPIC -I. test/support/process_watch.c -o build/338-parity/watch.so
PYTHONDONTWRITEBYTECODE=1 python3 - <<'PY'
import ctypes as c
import subprocess, sys
sys.path.insert(0, 'scripts')
from process_test_support import ProcessWatch, until
lib=c.CDLL('build/338-parity/watch.so')
lib.process_watch_open.argtypes=[c.c_uint64,c.POINTER(c.c_ulong)]
lib.process_watch_open.restype=c.c_void_p
lib.process_watch_now_ms.restype=c.c_uint64
lib.process_watch_alive.argtypes=[c.c_void_p,c.POINTER(c.c_bool),c.POINTER(c.c_ulong)]
lib.process_watch_wait.argtypes=[c.c_void_p,c.c_uint64,c.POINTER(c.c_ulong)]
lib.process_watch_kill.argtypes=[c.c_void_p,c.POINTER(c.c_ulong)]
lib.process_watch_close.argtypes=[c.POINTER(c.c_void_p),c.POINTER(c.c_ulong)]
for mode in ('exit', 'c-kill', 'python-kill'):
 p=subprocess.Popen(['build/338-debug/process_watch_testing','fixture-stdio'],stdin=subprocess.PIPE,stdout=subprocess.PIPE)
 py=None; w=c.c_void_p(); e=c.c_ulong(); alive=c.c_bool()
 try:
  assert p.stdout.readline()==b'READY\n'
  py=ProcessWatch(p.pid); w=c.c_void_p(lib.process_watch_open(p.pid,c.byref(e))); assert w
  assert py.alive() and lib.process_watch_alive(w,c.byref(alive),c.byref(e))==1 and alive.value
  assert lib.process_watch_wait(w,lib.process_watch_now_ms()+25,c.byref(e))==0
  try: py.wait(timeout=.025)
  except AssertionError: pass
  else: raise AssertionError('Python timeout was accepted')
  if mode=='exit': p.stdin.close()
  elif mode=='c-kill': assert lib.process_watch_kill(w,c.byref(e))==1
  else: py.kill()
  py.wait(); assert lib.process_watch_wait(w,lib.process_watch_now_ms()+5000,c.byref(e))==1
  assert not py.alive() and lib.process_watch_alive(w,c.byref(alive),c.byref(e))==1 and not alive.value
  p.wait(timeout=5)
  assert lib.process_watch_wait(w,0,c.byref(e))==1
  print('PASS identical Python/C process:',mode)
 finally:
  if py: py.close()
  assert lib.process_watch_close(c.byref(w),c.byref(e))==1
  if p.poll() is None: p.kill();p.wait()
  if p.stdin and not p.stdin.closed:p.stdin.close()
  p.stdout.close()

PY
```

A deliberately broken observer that always reports `alive=true` was also rejected
by `process.watch.alive-wait` after the real fixture exited (nonzero runner exit).
The unmodified runner passed Debug/Release and ASan/UBSan locally; LeakSanitizer
cannot run under this executor's ptrace, so local sanitizer runs disabled LSAN.
Each process scenario independently verifies native handle counts return to their
initial value. Windows execution is verified by the existing GitHub CI matrix.

## Import execution and interface migration (#362)

The `module-execution-interface-migration` label runs 33 import and 13 interface
C scenarios with the real existing host (including its linker-wrap counters).
Mappings, parity baselines and negative controls are in
`test/data/module-import-execution/README.md` and `test/data/module-interface/README.md`.
The independent archive/hash oracle verifies nonzero main indexes; the shared
barrier checks frozen execution after deleting the entire project. The Python
execution helper and format reference remain for the explicitly listed consumers.

Source diagnostic migration (#363): `module_source_diagnostics_scenarios` replaces
`check_module_source_diagnostics.py` with ten independent IDs (the relocation
pair remains one identity comparison). See
[`../data/module-source-diagnostics/README.md`](../data/module-source-diagnostics/README.md)
for mapping, Debug/Release/UBSan parity, negative controls and the explicit
Windows escaped-filename skip. The remaining Python consumers above retain the
shared compatibility helper until #394; no source-diagnostic consumer remains.

Information fields/merge migration (#364): `module_information_scenarios` uses
the shared execution, independent JSON/ZIP and significant-hash helpers for
206 independently selected IDs. The two retired Python runners exported no
helpers. See [mapping and reproduction](../data/module-information/README.md);
no production validator computes expected projection or merge results.

Dispatch/consistency migration (#365): `module_dispatch_consistency_scenarios`
replaces both Python runners with 144 stable IDs and explicit independent
bytecode inspection. See [mapping and validation](../data/module-dispatch-consistency/README.md).
`module_interface_fixtures.py` remains pending final cleanup after its last runner
consumer migrated in #374. The remaining execution helper consumers above are
not migrated here. Cleanup is
tracked by #394. The frozen consistency corpus already contains the original
external declaration instructions and is not rebuilt by a production helper.

Validation migration (#366): `module_validation_scenarios` uses shared execution,
independent JSON/hash/ZIP construction for real constructor libraries, and a frozen
malformed corpus sent unchanged to the real host. There are 62 stable IDs; see
[mapping and reproduction](../data/module-validation/README.md). The retired runner
exported no imported helpers; `module_interface_fixtures.py` remains for inspect
and merge until #394.

Writer migration (#367): `module_writer_scenarios` combines shared process/file
helpers, independent ZIP/profile/hash checks, reference-derived normalized parts,
and real atomic publication/fault checks. There are 59 stable IDs; see
[mapping and reproduction](../data/module-writer/README.md). No live helper is
exported by the retired runner. Frozen sample source paths and the optional
Git-baseline AST audit remain historical provenance, not working-tree consumers.

Native producer/library migration (#368) reuses module execution, language
execution, independent ZIP/SHA-256 and canonical JSON helpers. The two retired
Python runners exported no live imported helpers. See the
[build](../data/module-build/README.md) and [library](../data/module-library/README.md)
maps for 36 IDs, actual compiler/load evidence and bounded ELF alias mutation.

Inspector probe migration (#373) reuses the test-only independent transport in
`module_loading_owner.c` through its protocol-free command API. It captures both
streams and supplies a bounded copied stdin payload, can keep stdin open through
owner exit, and kills only the retained owner identity. It never adopts or kills
the cleaner/descendant tree under test. The loading protocol API remains intact.
Generic stdin follows `communicate` semantics for an already-closed reader;
other transport errors still fail. Independent Debug/Release controls cover early
reader exit, copied binary input, EOF versus held-open input, explicit owner kill,
wrong exit, deadline failure and rejected oversized input (14/14 controls).

## Module merge migration (#374)

`module_merge_scenarios` replaces the Python merge runner with 196 stable IDs.
`merge_terminal.*` supplies bounded, echo-disabled real POSIX terminal input,
callback-safe process cleanup and retained binary diagnostics.
`merge_constructor.*` compiles the original constructor fixture on the current
platform, builds its archive with the independent test oracle and verifies that
merge never loads it. No production behavior or hooks change. See
[the complete map and validation](../data/module-merge/README.md).

## Child-process migration (#375)

`child_process_scenarios` uses the existing independent raw owner transport,
never the process supervisor whose tree cleanup is being tested. Narrow test-only
transport extensions cover real file/PTY stdin, owner-only reap, line/EOF
observation, process-group termination and original zombie-sensitive PID
existence checks. Only identities pinned at a live readiness barrier are eligible
for deferred failure cleanup; late race output is not an authority to signal a
possibly reused numeric PID. See [the full map and validation](../data/child-process/README.md).
