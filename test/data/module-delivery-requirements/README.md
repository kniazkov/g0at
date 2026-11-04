# Runtime requirement migration (#389)

The source of truth is the unchanged `scripts/check_module_delivery_requirements.py`
at merged master `5602d440`. `manifest.json` records its full revision and SHA-256
and the SHA-256 of every frozen input. `main.goat`, both descriptors and the
`runtime_floor.c.txt` control are exact UTF-8 bytes originally written by that
script, including the original lack of a trailing newline where applicable.
The C runner copies them into its owned workspace. The control is copied under
its original name `runtime_floor.c`; it is compiled, never patched into a library.

## Old/new map

| Original entry | Stable C ID | Preserved assertions |
| --- | --- | --- |
| `audit` | `integration.module_delivery_requirements.audit` | Real producer baseline and extension flags, embedded runtime in every link, preprocessor macros, immutable archive selection, ABI/profile negatives, version boundaries and MSYSTEM spoofing |
| `prepare-floor` | `integration.module_delivery_requirements.prepare-floor` | Newer producer libc, ordinary/control same main hash and bytecode, actual `__memcmpeq@GLIBC_2.35`, ELF symbol inspection, native execution and constructor marker, sole delivered archive |
| `floor-image` | `integration.module_delivery_requirements.floor-image` | Actual archive requirement selects only Ubuntu 22.04 (2.35) or Debian bookworm-slim (2.36); unknown floors fail |
| `verify-floor` | `integration.module_delivery_requirements.verify-floor` | Exact libc equality, compiler-free off/auto/required execution, actual native binding/constructor markers, unchanged delivered bytes, no source/descriptor |
| Additional negative controls | `integration.module_delivery_requirements.controls` | Fail-closed migrated input/validation controls; do not replace any original phase |

All Windows compiler/selector checks remain on the original platform matrix,
including x86/x86-64 bitness, MSVCRT/UCRT family and host detection independent
of MSYSTEM/PATH. The explicit `[0]` no-extra-floor has no smaller valid version;
Windows OS values below 10 remain outside the supported profile schema.
The ordinary compiler flags remain baseline-only even when environment CFLAGS
or CXXFLAGS request native/AVX512. Explicit AVX2 or ARM SHA2 must affect generated
code and every embedded-runtime compilation together.

## Direct interface

```
module_delivery_requirements_scenarios ID GOAT WRAPPER SELECTOR CC BUILD DATA FORMAT_ROOT [--destination DIR] [--keep-work] [--timeout-seconds N]
```

Use `test/data` as DATA and `docs/module-format-v1` as FORMAT_ROOT. Prepare and
floor phases require `--destination DIR`; audit can use it to retain baseline
and extended archives for parity. BUILD must be writable even when the source
checkout is read-only. The three exact-floor phases remain external container
orchestration, not default host CTest tests. No host-based approximation or
successful skip is an acceptable replacement for an unavailable older runtime.

For Debug and Release, configure and build the target, run both direct checks,
then the temporary parity gate (the same gate runs through CTest on CI):

```sh
for mode in Debug Release; do
  build="build/requirements-$mode"
  cmake -S . -B "$build" -DCMAKE_BUILD_TYPE="$mode" -DGOAT_TEST_REQUIRE_PYTHON=ON
  cmake --build "$build" --parallel 2 --target module_delivery_requirements_scenarios
  for case in audit controls; do
    "$build/module_delivery_requirements_scenarios" "integration.module_delivery_requirements.$case" \
      "$build/goat" "$build/native_toolchain_testing" "$build/module_native_testing" gcc \
      "$build" test/data docs/module-format-v1
  done
  ctest --test-dir "$build" --output-on-failure -L module-delivery-requirements-migration
done
```

## Temporary parity gate and retirement

`test/support/module_delivery_requirements_parity.py` imports the unchanged
original and executes every assertion. Its only hooks retain original temporary
workspaces and check original source/descriptor/control writes against the
frozen inputs. No child result, compiler output or selector response is mocked.
Both runners build real artifacts independently. On Linux every ZIP member must
match byte-for-byte, including native-library bytes and metadata hashes; only
outer ZIP timestamps/header choices are excluded. Two independently retained
original runs reproduced complete ELF/member identity locally. Windows PE links
carry producer timestamps, as already documented by the #388 parity gate:
there each archive's complete native payload must match its recorded SHA-256,
then all metadata except those individually validated native hashes, all portable
bytes and member/compression lists must match. No ELF/PE byte is rewritten.

The temporary `integration.module_delivery_requirements.replay-audit` C mode
reads the untouched original `baseline.gbin` and `extended.gbin` artifacts via
`--destination`. It uses the same factored selector matrix as the independent
C audit. Thus the exact original bytes undergo both original and C selection,
including on Windows; no build result is replaced or mocked. Both independent
audits still execute all real compiler, flags and macro checks. The replay mode
is temporary and retires with the parity wrapper after the remote gate.
Any other difference is a failure to investigate, never permission to normalize
executable bytes or weaken producer assertions.

For floor preparation, both independently produced archives survive. The image
selection is checked by both implementations on both artifacts. Inside the chosen
older image CI rebuilds Goat **and the C scenario runner**, then runs original and
C verification on **each** archive. The checkout stays read-only and the build and
workspaces live in `/tmp/goat-floor`. On failure a separate writable `/evidence`
mount retains scenario/parity diagnostics without changing the read-only checkout. Both consumers independently check that
artifact bytes remain unchanged and that actual native functions execute.

The original, this temporary parity wrapper and all registrations remain until
all remote Build (11) and format-vectors (3) checks pass at one reviewed head.
Checklist step 71 remains unchecked. Removal needs a separate final CI pass.
No format, language, golden-vector, package (#391), or glibc/musl delivery (#390)
behavior is changed by this step.

The restricted local sandbox can fail process cleanup with ENOENT (2) or private
workspace ACL creation with error 95. Those are failures, not skipped scenarios
or parity evidence. Real scenario success and container execution must be
reported only when actually observed. Shared process/workspace, format-oracle and
executable-discovery helpers remain dependencies of later migration work.
