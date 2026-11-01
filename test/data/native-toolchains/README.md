# Native toolchain discovery migration (#379)

The independent runner invokes `native_toolchain_testing`, the existing native C
compiler wrapper and observation harness. No Python fake compiler is needed.
The wrapper delegates successful probes to a real compiler. The oracle only
reads process status/stdout and wrapper log bytes; it does not call production
compiler discovery, target selection, caching or timeout APIs to derive answers.
Each ID has an exclusive scenario work directory and fresh controlled compiler
copies. Commands retain the original 120-second external limit; the scenario has
a 480-second aggregate limit. Failed work/output is preserved; `--keep-work`
preserves successful diagnostics too. Shared corpus data is never modified.

CLI: `native_toolchains_scenarios ID HARNESS CC TARGET BUILD DATA [--keep-work]`.
`TARGET` is normally `host`; the musl gate retains its explicit target spelling.
Compiler lookup resolves the explicit CMake compiler or searches PATH for a bare
name, then pins the canonical executable path. Filesystem helpers are only
transport; all expectations below are independent observations.

## Original assertion mapping

Every ID below has prefix `integration.native_toolchains.`. Related observations
stay together where the Python script depended on one invocation/context.

| ID | Original contract |
| --- | --- |
| cache | Two `available=1` results; exactly two ` -std=c11` log entries for one context |
| reprobe | New process increases the number of compiler log lines |
| linux-relro | Linux argv contains exact NUL-terminated `-Wl,-z,relro,-z,now` |
| unicode-temp | Probe succeeds with all three temporary-root variables set to `временные файлы 漢字` |
| timeout-cache | Changed timeout produces four compile calls; GCC/Clang name and version captured |
| unsupported-target | `linux-unknown-gnu` never falls back to host |
| zero-timeout | Zero timeout diagnostic |
| absent-compiler | Missing explicit compiler diagnostic |
| link-failure | Preprocessing alone is insufficient; unavailable and controlled link failure |
| hang-query, hang-link | Shared one-second probe deadline reports unavailable and timed out |
| avx2 | Supported explicit profile retains `+avx2` |
| sha2 | Supported ARM profile succeeds and is discoverable by explicit host target; otherwise rejection |
| crypto, musl-crypto | GNU/musl unmodeled PMULL profile rejection |
| foreign-libc | Linux compiler headers cannot masquerade as opposite libc; unavailable and no selection |
| musl-trace | Musl rejects an unknown actual linked runtime |
| unknown-profile | Unknown CPU profile is never silently downgraded |
| path-first | First successful candidate only; every logged invocation is preferred compiler |
| path-fallback | Rejected preferred candidate permits stable next compiler |
| path-timeout | Timed-out preferred candidate reports timeout and permits stable fallback |
| path-unsearched | No hidden scanning outside PATH |
| path-list | All three controlled compilers enumerated |
| explicit-failure | Explicit failed compiler never selects fallback; exactly one candidate |

Non-Linux `linux-relro`/`foreign-libc`, non-musl `musl-trace`, and unsupported
`avx2` return explicit skip 77, matching original conditional assertions.
Unsupported SHA2 remains an exercised rejection, not a skip. Windows prefers
`gcc.exe` then `clang.exe`; POSIX prefers `cc` then `gcc`. On POSIX only known
compiler copies and resolved assembler/linker symlinks inhabit isolated PATH;
`COMPILER_PATH` explicitly retains internal compiler tools. Windows retains its
original PATH tail for runtime DLL discovery. Raw binary argv logs retain NULs;
normal text log parsing accepts native CRLF without changing the assertions.

Original Python was removed after full remote original/C parity passed at
`f5fcfe0021d4203b070646672d63737f766f8506`. Parity and final CI links are in PR #457.

## Local validation evidence

- Unchanged original Python passed 25/25 assertions in Debug, Release and UBSan.
- Final C runs: Debug 9 passed / 15 failed, Release 9 passed / 15 failed,
  UBSan 8 passed / 16 failed (24 IDs each). Every failed command had normal exit
  zero and supervisor `cleanup=2`; no failed output expectation or sanitizer
  diagnostic was hidden. This executor lacks `/proc/thread-self/children` and
  the existing cleanup limitation also affects these compiler descendants.
  These runs are **not green**; strict cleanup checks remain unchanged and full
  remote original/C parity was required and subsequently passed. Earlier runs varied in which commands
  triggered this environment limitation; no retry is presented as a full pass.
- Independent native-C fake harness controls: 122/122 across Debug and Release.
  Positive cases and mutations covered availability, exact matching-line cache
  counts (including duplicate tokens and unterminated/Unicode-separated lines),
  re-probing, compiler identity, diagnostics, linker failure, timeout flags,
  malformed UTF-8, nonzero status, exact NUL argv, platform skip predicates,
  SHA2 enumeration, opposite-libc rejection, musl trace, preferred/fallback
  selection, preferred-only calls, enumeration count and explicit failure.
  Fake observations do not call the production compiler-selection implementation.
- Four additional Debug/Release controls rejected a non-executable compiler file
  and a directory passed as an explicit compiler, before any probe ran.
- `clang-format` 18 dry-run and `git diff --check` passed. No runtime Python
  dependency or shared helper/fixture modification was introduced by this half.
