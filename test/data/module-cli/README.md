# Public module CLI scenarios (#371)

`test/src/functional/module_cli_scenarios.c` replaces
`scripts/check_module_cli.py` at baseline
`c6cf275684092f479a363d69fc445cd2f04d9bb7`. The read-only `.goat` files preserve
all original source strings byte for byte. `invalid-0.gmod` through
`invalid-10.gmod` preserve the original JSON inputs, including the escaped
`n\u0061me` duplicate key. Valid descriptors are constructed with the original
field order and Python `json.dumps` separators. There is no Python runtime,
production writer, or VM implementation inside the scenario runner: it invokes
the real `goat` CLI, reads ZIP members with the independent test oracle, and uses
the existing real compiler wrapper `native_toolchain_testing`.

## Old checks → CTest IDs

Every suffix below has prefix `integration.module_cli.` and label
`module-cli-migration`. The suite has **67 independently selectable IDs**.
Dependent state transitions remain together (for example byte-deterministic
replacement and dependency search precedence); independent invalid parameters
and raw-bytecode launch modes have distinct IDs.

| Original independent scenario / contract | CTest suffix |
| --- | --- |
| source discovery fails before initializer | `missing-include` |
| build-only, informational authors/producers, byte determinism, all run aliases, source independence, failed compilation preservation, successful replacement, no temporary residue | `basic` |
| invalid mode, target type, native unknown field, top-level unknown field, dependency name/type/version, author field, null version, false main, escaped duplicate name | `invalid-descriptor-0` … `invalid-descriptor-10` |
| valid descriptor overridden by CLI off; unused dependency warning; no native content | `override-valid` |
| off/required/auto overrides; missing compiler and unsupported target preserve prior artifact; warnings; native/off significant hash, bytecode and producer equivalence; all three execution modes | `native-overrides` |
| damaged native member rejected before execution even in off mode | `damaged-native` |
| Unicode project path, startup-relative output and environment paths, local/explicit/environment dependency precedence, relocated archive context, wrong/corrupt first match never falls back, failed rebuild preserves output | `search-precedence` |
| prospective named-root cycle rejected before publication | `cycle` |
| linked descriptor uses real source/base/output, independent startup cwd, explicit output identical | `descriptor-link` |
| source include/import graph, direct/run aliases, legacy compile/run | `direct-source` |
| descriptor launch preserves actual entry source directory and does not publish | `descriptor-context` |
| selected compiler genuinely probed, automatic listing, path shown, fresh-process reprobe, probe temporary cleanup | `listing` |
| failed compile/link probe is an error | `listing-link-fail` |
| explicitly missing compiler | `listing-missing` |
| empty compiler search path | `listing-path` |
| hung compiler query obeys explicit probe timeout | `listing-timeout` |
| invalid probe timeout values `0`, `-1`, `1.5`, integer overflow | `invalid-timeout-0` … `invalid-timeout-3` |
| automatic selection stops after preferred compiler succeeds | `auto-preferred` |
| automatic selection silently falls back after preferred failure | `auto-fallback` |
| enumeration includes both failed compiler diagnostics and successful fallback path | `listing-mixed` |
| explicitly selected failing compiler never falls back | `explicit-no-fallback` |
| off mode never probes selected executable | `off-no-probe` |
| selected wrapper observes real probing/compilation environment; required execution; temporary cleanup | `controlled-native` |
| descriptor off mode applies without CLI override, no compiler invocation/native member | `descriptor-off` |
| absent descriptor mode defaults auto; real native member/call; required execution; temporary cleanup | `default-auto` |
| dependency native code cannot satisfy direct-source required mode; auto still executes initialization and call | `dependency-native-required` |
| effective default-auto report has positive succeeded/bound counts; archive/source report collisions preserve bytes | `reports` |
| known extensions are case-insensitive with distinct basenames | `uppercase` |
| output cannot overwrite unusual-suffix source | `source-collision` |
| hardlinked output/source collision preserves both names | `hardlink-output` |
| output directory and sentinel preserved | `directory-output` |
| output symlink and its target preserved | `symlink-output` |
| existing direct source and split legacy compilation remain supported | `legacy` |
| raw bytecode accepted by direct, `run`, `--run`; input unchanged | `raw-valid-0`, `raw-valid-1`, `raw-valid-2` |
| empty, seven-byte prefix, magic-only, truncated payload, old magic, invalid version; each under three launch spellings; specific English diagnostic and unchanged bytes | `raw-invalid-0` … `raw-invalid-17` |

For `raw-invalid-N`, malformed variant is `N / 3` in table order and launch
spelling is `N % 3` (direct, `run`, `--run`). Each malformed input is derived from
fresh genuine `GOATPG01` legacy output, with independently decoded little-endian
code length, not a hard-coded bytecode substitute.

## Execution and portability

```sh
cmake -S . -B build/cli-Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/cli-Debug --target module_cli_scenarios
ctest --test-dir build/cli-Debug --output-on-failure -L module-cli-migration
build/cli-Debug/module_cli_scenarios integration.module_cli.basic \
  build/cli-Debug/goat build/cli-Debug/native_toolchain_testing /usr/bin/cc \
  build/cli-Debug test/data --keep-work
```

Repeat in a separate Release build. The real compiler argument may also be a
PATH name. The runner resolves executables without a shell, snapshots inherited
environment, clears `GOAT_MODULE_PATH` except in explicit precedence cases, and
sets per-child cwd to its unrelated directory. Compiler isolation preserves the
original platform policy: Linux prefers `cc` then `gcc` with isolated assembler
and linker paths; Windows prefers `gcc.exe` then `clang.exe` and retains system
PATH entries needed by the compiler runtime.

All assertions use fail-fast scenario contracts and remain active in Release.
The existing compiler wrapper's assertions must also remain active in Release
because they contain I/O side effects. All subprocesses share a 180-second
cumulative scenario deadline; the deliberately hanging-probe scenario retains
the original 30-second outer limit. Probe temporary cleanup uses the shared
independent `process_watch_until` helper, bounded by the original 10 seconds and
the remaining scenario deadline. Complete commands, outputs, errors and failures
are retained under a unique build-local workspace. `--keep-work` also retains
successful work. Failures/crashes/timeouts/cleanup errors never count as expected
CLI input errors; expected failures require a normal nonzero exit and a diagnostic.

Windows symlink fixtures return explicit SKIP (77) only for the documented missing
symlink privilege (error 1314). Hardlink fixtures explicitly skip only known
unsupported-filesystem/API or Windows missing-privilege errors; other unexpected
filesystem errors fail. Normal
CI is strict. The original local-only `--allow-unsupported-acl` accommodation is
available only when replacement actually reports unsupported ACL reads and Linux
`getxattr` independently confirms `ENOTSUP`/`EOPNOTSUPP`; it reports the limitation,
retries a fresh publication, and returns 77 rather than claiming replacement
coverage. It is never supplied by CI. No production format, language, filesystem,
or compiler implementation is changed by these scenarios.

Compiler-call assertions match executable basenames at path boundaries, not arbitrary
substrings: a `gcc` call cannot satisfy the preferred `cc`-only contract.

## Migration verification

The original Python suite passed all 115 checks in both Debug and Release,
strictly, with real compiler, symlink and hardlink fixtures and no ACL opt-in.
All 28 source fixtures match the original strings exactly. The original
Debug/Release command streams also match after only workspace/build-path
normalization.

Independent negative self-tests reject 23 deliberately wrong results in each
configuration, with four corresponding positive controls each (54/54 total).
They cover wrong exit/output/diagnostics, invalid bytecode accepted or rewritten,
missing artifacts/producers/native metadata, erased compiler logs, missing
reprobe/unwanted probe, incorrect fallback warnings and clobbered archives,
source files, hardlinks, directory sentinels, symlink targets and reports.
They also verify rejection of negative report counters and that `gcc` cannot
masquerade as a preferred `cc` invocation.

Local direct CTest was additionally attempted in Debug, Release and UBSan.
This executor lacks `/proc/thread-self/children`; when a native cleanup service
is still pending, the existing subprocess supervisor reports `cleanup_error=2`
(ENOENT) after a normal CLI exit. These cases remain failures, not ignored or
skipped. No production workaround or relaxed cleanup assertion was introduced.
Full direct execution therefore remains a mandatory strict GitHub CI gate on
real Linux/Windows/musl runners; authoritative links are in the PR description.

Python/C parity is complete for all 115 original invocations in both builds:
normalized argv/cwd/environment, exact source/descriptor/archive inputs, exits,
complete stdout/stderr and changed outputs are matched together per invocation.
All 23 archive-producing/changing commands produce byte-identical archives.
The deliberately damaged ZIP has equivalent member bytes but independent ZIP
header timestamps/attributes; the original Python assertions were additionally
replayed against the exact C-produced damaged archive in both builds, confirming
identical failure diagnostics and immutable input. Supplemental capture uses an
external transparent recording proxy only for migration evidence, not as a
repository test dependency. Debug capture passed 67/67; Release passed 66/67 on
its full capture, and the single cleanup-limited native-overrides scenario passed
one targeted capture retry. The direct-C cleanup limitation above is not hidden
by these parity captures. The Python runner was deleted only after this comparison.
