# Native access migration (#384)

The two `.goat` files freeze the exact UTF-8 strings passed to `Path.write_text`
in unchanged `test/src/native/checks/access.py`. Neither contains a newline, so
Windows text-mode writes produce the same bytes. Expected output is a fixed
literal in the C checker, not obtained from Goat or another production oracle.

Run `native_access_scenarios all|ID|--list GOAT REPOSITORY BUILD DATA [--keep-work]`.
DATA is the read-only `test/data` root. REPOSITORY is the inherited command cwd
used by the original manifest. The inherited CC value is preserved verbatim,
including absent/empty/relative values; only compiler-fallback replaces it with
`goat-access-test-missing-compiler`. Every command uses GOAT_LANGUAGE=en.

| ID suffix (`integration.native_access.` prefix) | Original assertion |
| --- | --- |
| denied-none | `blocked.goat`, optimize none, native off: exact ACCESS_DENIED stdout |
| denied-all | Same, optimize all |
| denied-compile | Native required compilation fails; empty stdout; access diagnostic substring; no blocked.gbin of any kind |
| retry-off | Exact retry/argument/access/copied-function stdout; report parses as key=value |
| retry-auto | Same stdout, attempts=2, succeeded=1, retries=1 |
| retry-required | Same stdout and counters |
| persisted | Compile native required, empty stdout; execute persisted retry.gbin with required and exact stdout |
| compiler-fallback | Missing CC with auto; exact stdout and nonempty stderr |
| control-output | Full checker accepts controlled baseline then rejects altered output |
| control-report | Full checker accepts controlled baseline then rejects retries=0 |
| control-fallback | Full checker accepts controlled baseline then rejects empty fallback stderr |
| control-bytecode | Full checker accepts controlled baseline then rejects forbidden blocked.gbin |
| all | First eight original cases in one aggregate deadline |

The controls re-execute the same runner as a controlled command producer, then
require the full checker to fail at the intended assertion. A baseline must pass
first and the negative's named diagnostic must be present. Unrelated command,
timeout, or process-tree-cleanup failures cannot masquerade as accepted controls.
They do not replace real Goat parity runs. All checks remain active in Release.

Each registry invocation owns a unique workspace, and the eight `all` cases have
separate subdirectories. Inputs are copied; fixtures are never modified. Command
captures, byte mismatches and execution logs survive failures; `--keep-work`
retains successful work too. Commands have 120-second deadlines and each registry
invocation (including `all`) has a shared 300-second aggregate deadline. A CTest
outer timeout must allow additional time for process-tree cleanup. Universal
newline decoding matches Python text=True; whitespace is not trimmed.

Keep the original Python checker until full Debug and Release Linux, Windows and
musl parity is green remotely. This executor may lack
`/proc/thread-self/children`; native compiler cleanup then reports cleanup=2.
That failure is not skipped, retried away or treated as success.

## Local validation (2026-11-03)

Built both `build/migration384-debug` and `build/migration384-release`. In each,
all eight non-compiler cases passed: denied-none, denied-all, denied-compile,
retry-off and all four controlled negative cases. In each, retry-auto,
retry-required, persisted and compiler-fallback stopped on the existing
`cleanup=2` supervisor failure despite Goat exiting zero. The aggregate `all`
invocation in each configuration likewise stopped at retry-auto with cleanup=2.
No cleanup check was changed and these failures were not counted as parity passes.
Full real-compiler C parity remains a remote CI requirement. The unchanged
Python access and libm checkers passed separately in both Debug and Release,
including byte comparison of generated sources and host drivers with frozen inputs.

The C source also compiles with `-std=c11 -Wall -Wextra -Werror` and is formatted
with the repository's clang-format 18 configuration. Frozen source SHA-256:

- blocked.goat: `120d763144df17b1ef414265a0e9965c43880e1f75bc1d1f07ea32bb37c5ac72`
- retry.goat: `03de892f36437bf1148a5fef9d004d1acd790015dec1f9d5ec164c100aed22a2`
