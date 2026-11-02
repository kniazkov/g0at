# Native requirements migration (#379)

## Independent corpus

`scenarios.json` freezes all 70 image checks from
`scripts/check_native_requirements.py` at
`60bed8c3effa6a21978544605cb39969dd222b9d`. The original Python `elf`, `pe`,
`musl_image`, and `musl_module` builders and every mutation loop were executed
unchanged; only `check` was replaced by a byte recorder. Linux runtime probes
were omitted from this extraction and remain real subprocess scenarios.
No production parser, emitter, compiler, or current output generates expected
results. Source/table hashes and extraction method are in `provenance.json`.

All 68 deduplicated blobs have SHA-256 filenames, verified against their bytes.
Each case freezes the exact dependency directory snapshot, including leftover
files from preceding original checks. It is copied into a fresh scenario-owned
work directory; the checked-in corpus is read-only. Scoped `-text` prevents Git
from rewriting binary fixtures. Python slice-assignment resizing is preserved
exactly, including PE `__p__iob` and the musl aarch64 banner's longer spelling
followed by `pop()`. No C reconstruction silently corrects those layouts.
Synthetic musl images test only parser layouts, never claim native musl execution.

## Runtime and acceptance

All five Linux stack probes have stable IDs: `stack-main` and `stack-worker`
expect `1\n`; `stack-small` and `stack-limited` expect `0\n`; `stack-blocked`
accepts only `0\n` or `not-musl\n`. Other operating systems explicitly skip
these five IDs (77), not the image corpus. The blocked non-musl answer remains
an observed pass, just as Python accepted it without incrementing its counter.

`stack-limited` launches a supervised runner helper which sets both soft and
hard `RLIMIT_STACK` to 128 KiB, then `execv`s the unchanged host's `--stack-main`.
The parent never changes its own limit, and exec preserves the fixture status.
No shared test/production API or fixture change is needed.

Every subprocess retains the original 10-second cap; setup and teardown share
an outer 20-second scenario bound (CTest 30 seconds). Stack stdout is exact after
Python-compatible universal-newline conversion; stderr is unspecified. Image
checks retain success versus nonzero rejection and every required substring
in stdout concatenated with stderr (not merely one token). Outputs without an
original substring condition remain unspecified. The supervisor additionally
rejects signals, timeouts and cleanup failures rather than mistaking them for
an intentional parser rejection. Failure artifacts and `--keep-work` use the
standard scenario context; no subprocess cwd or environment mutation leaks.

## Mapping

Each ID below has prefix `integration.native_requirements.`. Rows retain the original check and loop order; each loop iteration is independent.

| ID suffix | Target | Acceptance | Image bytes |
| --- | --- | --- | ---: |
| elf-baseline | glibc | success + `3.2.0.0 2.9.0.0` | 4096 |
| elf-dependency-228 | glibc | success + `2.28.0.0` | 4096 |
| elf-dependency-240 | glibc | success + `2.40.0.0` | 4096 |
| elf-dt-relr | glibc | success + `2.36.0.0` | 4096 |
| elf-future | glibc | reject | 4096 |
| elf-private | glibc | reject | 4096 |
| elf-gcc | glibc | reject | 4096 |
| elf-missing-dependency | glibc | reject | 4096 |
| elf-wrong-machine | glibc | reject | 4096 |
| elf-no-note | glibc | reject | 4096 |
| elf-truncated | glibc | reject | 30 |
| elf-section-overflow | glibc | reject | 4096 |
| elf-version-overflow | glibc | reject | 4096 |
| elf-big-endian | glibc | reject | 4096 |
| musl-baseline | musl | success + `2.6.39.0 1.2.5.0` | 4096 |
| musl-as-glibc | glibc | reject | 4096 |
| glibc-as-musl | musl | reject | 4096 |
| musl-no-dependency | musl | reject | 4096 |
| musl-no-banner | musl | reject | 4096 |
| musl-duplicate-release | musl | reject | 4096 |
| musl-dev-release | musl | reject | 4096 |
| musl-short-release | musl | reject | 4096 |
| musl-version-overflow | musl | reject | 4096 |
| musl-unsupported-major | musl | reject | 4096 |
| musl-load-offset-overflow | musl | reject | 4096 |
| musl-load-filesz-overflow | musl | reject | 4096 |
| musl-load-memsz-small | musl | reject | 4096 |
| musl-flags-none | musl | reject | 4096 |
| musl-flags-write | musl | reject | 4096 |
| musl-flags-read-write | musl | reject | 4096 |
| musl-flags-read-execute | musl | success | 4096 |
| musl-aarch64 | musl-aarch64 | success + `3.7.0.0 1.2.5.0` | 4096 |
| musl-wrong-machine | musl | reject | 4096 |
| musl-glibc-libc | musl | reject | 4096 |
| musl-glibc-versions | musl | reject | 4096 |
| pe-msvcrt | msvcrt | success + `10.0.0.0 0.0.0.0` | 4096 |
| pe-iob | msvcrt | success | 4096 |
| pe-p-iob | msvcrt | success | 4096 |
| pe-kernel-GetCPInfo | msvcrt | success | 4096 |
| pe-kernel-GetStringTypeW | msvcrt | success | 4096 |
| pe-kernel-MultiByteToWideChar | msvcrt | success | 4096 |
| pe-kernel-WideCharToMultiByte | msvcrt | success | 4096 |
| pe-ucrt-_create_locale | ucrt | success | 4096 |
| pe-ucrt-_free_locale | ucrt | success | 4096 |
| pe-ucrt-_strtod_l | ucrt | success | 4096 |
| pe-ucrt-_vsnprintf_l | ucrt | success | 4096 |
| pe-msvcrt-__lc_codepage | msvcrt | success | 4096 |
| pe-msvcrt-__p___mb_cur_max | msvcrt | success | 4096 |
| pe-msvcrt-_setjmp3 | msvcrt | success | 4096 |
| pe-msvcrt-localeconv | msvcrt | success | 4096 |
| pe-msvcrt-longjmp | msvcrt | success | 4096 |
| pe-msvcrt-strchr | msvcrt | success | 4096 |
| pe-msvcrt-___lc_codepage_func | msvcrt | success | 4096 |
| pe-msvcrt-___mb_cur_max_func | msvcrt | success | 4096 |
| pe-msvcrt-_setjmp | msvcrt | success | 4096 |
| pe-private-__intrinsic_setjmp | ucrt | success | 4096 |
| pe-private-longjmp | ucrt | success | 4096 |
| pe-private-strchr | ucrt | success | 4096 |
| pe-unknown-symbols | msvcrt | reject + `msvcrt.dll!future`, `KERNEL32.dll!Unknown` | 4096 |
| pe-inventory-truncated | msvcrt | reject + `[inventory truncated]` | 4096 |
| pe-ucrtbase | ucrt | success | 4096 |
| pe-ucrt-api-runtime | ucrt | success | 4096 |
| pe-msvcrt-as-ucrt | ucrt | reject | 4096 |
| pe-unknown-dll | msvcrt | reject | 4096 |
| pe-wrong-machine | msvcrt | reject | 4096 |
| pe-os11 | msvcrt | success + `11.0.0.0` | 4096 |
| pe-delay-import | msvcrt | reject | 4096 |
| pe-future-symbol | msvcrt | reject + `msvcrt.dll!future` | 4096 |
| pe-ordinal-import | msvcrt | reject | 4096 |
| not-executable | glibc | reject | 14 |

## Validation

Run `ctest -L native-requirements-migration --output-on-failure` in Debug,
Release and UBSan; compare the unchanged original Python before deleting it.
Full remote parity includes Linux glibc/musl and Windows MINGW32/MINGW64/UCRT64.
Local validation results and independent controls are recorded below.

Local Linux glibc validation passed:

- Debug, Release and UBSan: 75/75 C IDs each, including all stack modes.
- Unchanged Python: 74/74 reported in all three configurations; its additional
  blocked probe also ran and accepted `not-musl`, but does not increment the
  original counter on non-musl hosts. Thus 75 C IDs do not add or omit a check.
- Independent fake-host controls: 40/40 across Debug and Release. They cover wrong statuses, missing version tokens,
  missing one of two symbol diagnostics, missing truncated-inventory marker,
  signal-versus-intentional rejection, exact stack values/status, universal
  newline conversion, both blocked alternatives, and constrained-stack launch.
  A status of 23 is deliberately accepted for an image rejection, preserving
  the original nonzero policy. Tokens split across stdout/stderr are accepted
  only after the original ordered concatenation.
- Captured output must be valid UTF-8 before comparing either stream, using a
  test-only scalar validator. This retains strict decoding in UTF-8 CI locales
  and deterministically rejects malformed output even without required tokens.
  A legacy non-UTF-8 Windows Python locale could decode additional byte values;
  the fixture's actual diagnostics are ASCII, so this stricter malformed-output
  policy does not alter any frozen case. Both malformed-stream controls reject.

Full remote original/C parity passed at `f5fcfe0021d4203b070646672d63737f766f8506`
before Python removal. Parity and final CI links are recorded in PR #457.
