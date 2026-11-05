# Runtime requirement migration (#389)

The original source was `scripts/check_module_delivery_requirements.py`
at merged master `5602d440`, retained through the completed parity gate below. `manifest.json` records its full revision and SHA-256
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
and extended archives for diagnosis. BUILD must be writable even when the source
checkout is read-only. The three exact-floor phases remain external container
orchestration, not default host CTest tests. No host-based approximation or
successful skip is an acceptable replacement for an unavailable older runtime.

For Debug and Release, configure and build the target, run both direct checks,
then the registered C tests (the same checks run through CTest on CI):

```sh
for mode in Debug Release; do
  build="build/requirements-$mode"
  cmake -S . -B "$build" -DCMAKE_BUILD_TYPE="$mode"
  cmake --build "$build" --parallel 2 --target module_delivery_requirements_scenarios
  for case in audit controls; do
    "$build/module_delivery_requirements_scenarios" "integration.module_delivery_requirements.$case" \
      "$build/goat" "$build/native_toolchain_testing" "$build/module_native_testing" gcc \
      "$build" test/data docs/module-format-v1
  done
  ctest --test-dir "$build" --output-on-failure -L module-delivery-requirements-migration
done
```

## Completed parity gate and retirement

[PR #470](https://github.com/kniazkov/g0at/pull/470) passed all 14 checks at exact
head `c285eff24cdc21be70abe383f383a550c14f7546`:
[Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41788518179) and
[format-vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41788518149).
All seven platform logs confirmed original/C Debug and Release audit parity.
All four Linux variants confirmed preparation, image selection and both original
and C verification of both independently produced artifacts inside the selected
exact older runtime. These were real producer artifacts with actual ELF dependency
and native execution, not relabeled metadata fixtures.

The removed parity wrapper executed every original assertion and checked its
source/descriptor/control writes against these frozen inputs. Linux compared
every archive member including native bytes. Windows independently validated each
native payload hash and compared remaining metadata, portable bytes and member
lists because independent PE links carry timestamps. The temporary C replay mode
applied the same selector matrix to untouched original artifacts. No build output
or selector result was mocked and no executable bytes were normalized.

The original Python runner, temporary wrapper, original/parity registrations and
temporary replay mode were removed only after that complete gate. Final retirement
`847a7811` passed [Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41797598329)
and [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41797598431);
merged-master evidence is in the [migration checklist](../../../docs/c-tooling-migration-checklist.md).

The final workflow uses only the C runner for prepare-floor, floor-image and
verify-floor. Inside the selected older image it rebuilds Goat **and the C runner**.
The checkout remains read-only; build/workspaces use `/tmp/goat-floor`, and a
separate writable `/evidence` mount retains failure diagnostics. Exact libc
version equality, compiler-free VM/auto/required execution, real constructor
markers and unchanged delivered bytes remain mandatory. This runner has no
Python dependency. The remaining transitional Python consumers were subsequently
retired in #390–#394.

No format, language, golden-vector, package (#391), or glibc/musl delivery (#390)
behavior is changed by this step.

The restricted local sandbox can fail process cleanup with ENOENT (2) or private
workspace ACL creation with error 95. Those are failures, not skipped scenarios
or parity evidence. Real scenario success and container execution must be
reported only when actually observed. Shared process/workspace, format-oracle and
executable-discovery helpers remain dependencies of later migration work.
