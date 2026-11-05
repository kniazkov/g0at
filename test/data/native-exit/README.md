# Native exit services (#382)

`native_exit_scenarios` preserves the 28 checks from `check_native_exit.py`:
exit-service prefix compatibility and 27 native/legacy execution cases. It runs
the real `module_dispatch_testing` host; no production exit/adapter function is
used to derive expected status, output or error policy.

## Reproduction

```
cmake --build BUILD --target native_exit_scenarios
ctest --test-dir BUILD -R '^integration.native_exit\.' --output-on-failure
native_exit_scenarios integration.native_exit.code-7 HOST CC REPOSITORY BUILD DATA FIXTURES [--keep-work]
```

`DATA` is the repository's `test/data`, `FIXTURES` is `test/data/native-exit`;
`REPOSITORY` supplies the public ABI include directory and existing
`test/src/functional/native_exit/provider.c`. `HOST` is `module_dispatch_testing`.
Each invocation owns an exclusive scenario workspace; failures retain diagnostics
and captures, while `--keep-work` also retains successes. Fixtures are read-only.
Prefix execution has 10 seconds, each ABI invocation 15 seconds, and legacy
compilation 60 seconds; the enclosing scenario has 90 seconds. Checks remain
active against Release libraries.

The real legacy provider is compiled separately for each legacy case with the
original C11/warning/pedantic/O2/native-build/include/shared flags, plus POSIX PIC.
Windows uses its original shared-library flags without adding POSIX options.
Compiler PATH lookup preserves the selected symlink spelling (including a bare
or relative wrapper name); failure to locate it retains the original executable
fallback. Subprocesses inherit the invoking cwd and environment, as before.

## Stable-ID mapping

All IDs below have prefix `integration.native_exit.`.

| ID | Original check |
| --- | --- |
| exit-prefixes | Exit service context prefixes: status 0 and byte-stripped `OK`; stderr unconstrained |
| code-0, code-7, code-255 | Native exit codes, `before` only, no host metrics or subsequent code |
| invalid-0 … invalid-5 | -1, 256, real, boolean, character, string rejected as INVALID_ARGUMENT; finally and subsequent code run |
| arity-0, arity-2 | Invalid argument count reported through host exception service |
| invalid-then-valid | Invalid exit caught, subsequent valid native exit terminates |
| native-initializer | Exit during initialization rejected; later valid native exit succeeds |
| native-no-finally | Valid native exit does not return through catch/finally |
| exit-forged, exit-ignored | Forged/ignored host failure becomes terminal adapter error |
| legacy-call-0, legacy-call-7, legacy-call-255 | Legacy ABI forwards valid exit and never returns |
| legacy-invalid | Legacy invalid argument is catchable; finally and later code run |
| legacy-initializer | Legacy callback during initializer rejects exit, later call exits |
| legacy-forged-257, legacy-forged-258 | Forged and ignored legacy host error are terminal, with metrics |
| loop-vm, loop-native, block-vm, block-native | Noncooperative spinning/blocked worker interrupted through VM/native exit; `entered` proves the explicit worker readiness handshake |

For ABI calls, status is exact and stdout is strictly UTF-8 decoded with **only**
CRLF-pair normalization. Terminal modern cases require the binary stderr substring
`native adapter failed`; terminal legacy cases require `METRICS `. Ordinary
status-zero non-exit cases also require `METRICS `. Every other case requires
empty stderr. Stderr otherwise remains binary and need not be UTF-8. Prefix
stdout uses Python's ASCII byte-whitespace stripping, not Unicode stripping.

## Frozen inputs

`scenarios.json` records all 27 expanded cases, signatures/actions in their exact
original order, function IDs, expected output, status and error-policy flags.
The 56 source/config/include literals were compared against the **unmodified**
original `run()` implementation. Config JSON bytes match Python `json.dumps`,
except that its per-run absolute legacy library path is represented by
`$LEGACY_LIBRARY`; only that token is escaped/replaced in the work copy.
Nonlegacy `legacyLibrary` remains JSON null. Dynamic path escaping may use raw
UTF-8 instead of `ensure_ascii` escapes, preserving the decoded path exactly.

`provenance.json` records the original script SHA-256 and every literal hash.
Windows work-file writes retain original text newline translation; repository
fixtures are protected by scoped `-text` attributes. No Python fake compiler or
runtime Python helper is introduced.

## Local verification

- Real C runner: **28/28** in Debug, Release and UBSan.
- Unchanged Python: **28/28** in Debug, Release and UBSan against the corresponding
  real host and compiler, including all legacy ABI and worker interruption cases.
- Independent native-C fake controls: **238/238** across Debug and Release.
  Every case has positive, wrong-status, extra-output and wrong-stderr controls;
  additional checks cover invalid stdout UTF-8, accepted raw stderr bytes,
  CRLF versus lone CR, prefix stripping, compiler failure and bare/relative
  symlink-wrapper basename preservation.
- Four additional hanging-host controls rejected at the actual 10-second prefix
  and 15-second ABI limits in both Debug and Release, with bounded process cleanup.
- Independent original-run corpus audit: **27 cases / 56 files** match, normalizing
  only the dynamic legacy path. All 56 recorded hashes match the frozen bytes.
- `clang-format` 18 dry-run and `git diff --check` passed.

Full remote platform parity was required before original Python deletion and
is recorded below; these local Linux results do not claim Windows or musl execution.


## Remote parity gate and Python removal

The corrected parity head `130e06b4d4820b4d5633dbb8a709bd27a00a1fd0` in
[PR #460](https://github.com/kniazkov/g0at/pull/460) passed the full
[Build 41438905339](https://github.com/kniazkov/g0at/actions/runs/41438905339)
(11/11 jobs) and [module vectors 41438905517](https://github.com/kniazkov/g0at/actions/runs/41438905517)
(3/3 jobs). An initial Windows compile failure was corrected by renaming the
native-exit helper `boolean` to `boolean_field`, avoiding the Windows SDK
`rpcndr.h` typedef collision; parity evidence refers only to the corrected head.

Audited logs from all nine runtime jobs each show two original native-exit suite
passes, two original native-values suite passes, 56 native-exit C passes and six
native-values C passes: **36 original aggregate passes and 558 C scenario passes**
across Debug/Release, Linux GCC/Clang x86-64/AArch64, musl and Windows
MINGW32/MINGW64/UCRT64.

Only after this gate, the two original Python scripts and temporary parity
registrations/labels were removed. All 31 permanent C IDs (28 exit + three values)
and their platform coverage remain. Historical input names/hashes above are
preserved for provenance. The final removal commit must pass its own complete CI
before merge.
