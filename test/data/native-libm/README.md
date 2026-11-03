# Frozen native libm cases (#384)

`scenarios.json` maps all original `test/src/native/checks/libm.py` observations
into 58 independent C scenario IDs. `provenance.json` records the original script
SHA-256 and the SHA-256 of each of its 62 generated input files. These are original
UTF-8/LF bytes, not rewritten approximations. The C runner expands LF to CRLF when
writing on Windows, matching `Path.write_text`. Host fixtures have a `.txt` suffix
only to exclude them from C source formatting; work files retain `host-*.c` names.

Fixture capture executed the unchanged original generator with mocked subprocess
results and intercepted its actual file writes, including all eight instantiated
host templates and the min/max zero-sign special cases. No Goat invocation was
used to compute expected answers. An independent review replayed the original
script and verified all 62 fixture byte streams. The committed directory contains
only data and provenance, with no generator or Python runtime dependency. The
original Python suite remains for Debug/Release Linux, Windows and musl parity
gates. To audit provenance, verify the original script hash and the individual
fixture hashes in `provenance.json`, then compare against the original generator's
writes before platform newline translation.

## Runner interface and coverage

`native_libm_scenarios all|ID|--list GOAT REPOSITORY BUILD DATA [--keep-work]`

DATA is the existing `test/data` directory; fixtures are under DATA/native-libm.
GOAT and REPOSITORY are resolved absolutely. The compiler remains the unsplit `CC`
environment value, or `gcc` if absent, exactly as in the original. Goat children inherit the environment with `GOAT_LANGUAGE=en`; compiler and host
children inherit the unchanged environment. All use REPOSITORY as cwd, preserving
relative CC resolution. Compiler flags are the original `-std=c11`, both include
paths, `-Wall -Wextra -Werror -O2`, host source, `-lm -o` and executable. These host
compilations do not inherit CMake Release's `-DNDEBUG`, so original `assert` checks
stay active in every build configuration. The scenario runner uses always-active
requirements, never C `assert`.

Positive IDs use the prefix `integration.native_libm.`:

- `zero-{min,max}-{none-off,all-off,all-required}`: six fixed signed-zero contracts
  covering direct folding and function calls. Expected stdout is independent of
  the VM and native implementation.
- `unary-{abs,acos,asin,atan,cbrt,ceil,cos,cosh,exp,exp2,expm1,floor,int,log,log10,log1p,log2,round,sign,sin,sinh,sqrt,tan,tanh,trunc}`:
  all 25 original unary value sets, including INT64_MIN and signed zero.
- `binary-{max,min,pow,fmod,hypot}`: all five original pair sets.
- `semantics-{extra_bool,extra_effect,alias,effects,effects_binary,atan2,atan_extra_arg,abs_extra_arg}`:
  native/VM equivalence and reports for argument effects, extra arguments and aliases.
- `semantics-{replace,nested_shadow}`: auto-mode fallback/VM equivalence.
- `reject-{atan_invalid,atan_missing,abs_invalid,abs_missing}`: fixed `caught\n`
  VM result and auto-mode equivalence.
- `adapters-{cos,sqrt,pow,fmod,min,max,atan,abs}`: compile and invoke all unchanged
  generated host drivers. They independently use system libm, preserve explicit
  min/max signed-zero policy, and cover INT64 boundaries, domain errors, NaN,
  infinities and signed zero on values absent from source observations.

Every required native report retains the original contract: positive Python-style
integer succeeded, literal omitted=0 and retries=0, exact attempts/succeeded text
equality. The report parser preserves splitlines Unicode boundaries and last-key
wins behavior. No report values or numeric expectations come from production
helpers. VM/native equivalence is used only where the original used it.

Each Goat command has a 30-second deadline, compiler a 60-second deadline, and host
a 30-second deadline. Each independently selected positive scenario has a
150-second total cap. `all` (also `integration.native_libm.all`) runs exactly the
original 58 positive observations, in original order, under one shared 300-second
aggregate deadline. Its separate per-ID directories live inside its unique
framework work directory. All commands keep complete build-scoped output and
status diagnostics; `--keep-work` retains successful work too. Any supervisor
cleanup error is a failure, including restricted-environment cleanup=2.

Additional stable negative-control IDs:

- `negative-output`: accepts a good fixed baseline, rejects changed signed zero,
  extra bytes and a nonzero status through the same output acceptance predicate.
- `negative-report`: accepts ordinary and Unicode/underscore/signed positive
  baselines, rejects corrupt/missing counts and malformed lines through the same
  report predicate.
- `negative-host`: generates the real cos adapter and compiles the original host
  checks with assertion failure converted to normal status 86. A good baseline
  must first exit zero. A second host corrupts the adapter return value before
  those same independent libm checks and must exit exactly 86, which the normal
  host acceptance predicate rejects. Crashes, timeouts and cleanup errors cannot
  masquerade as the intentional rejection. Its total cap is 240 seconds.

The negative controls are deliberately excluded from `all`, and are registered as
separate CTest cases. Integration requires the standard language execution/test
support libraries plus yyjson; no production math oracle is linked or consulted.

## Local verification (2026-11-03)

A single full pass of the final checker produced **30/61 Debug** and **32/61
Release** passing CTest cases. Every remaining failed case reports the existing
supervisor `cleanup=2` with the child exiting zero; none is counted as a parity
pass. All three negative controls pass in both configurations, including the
real generated-host baseline and intentional corruption. Aggregate execution
also stops on this cleanup failure. No assertion, timeout or cleanup requirement
was weakened, and failed cases were not retried merely to obtain a pass.

Both unchanged Python scripts pass in Debug and Release; the temporary original
parity gate also compares their generated source/host bytes to these frozen
inputs. Independent review matched all 62 libm fixtures and both access sources
against the original Python writes. Strict `-std=c11 -DNDEBUG -Wall -Wextra
-Werror` compilation, repository formatting, and CMake registration checks pass.
Full cross-platform original/C parity in CI is required before Python removal.
