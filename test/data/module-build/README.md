# Native module producer migration (#368)

`test/src/functional/module_build_scenarios.c` replaces
`scripts/check_module_build.py` at baseline `64ef1dc4`. Ten stable IDs cover the
original six projects and all fourteen host calls. Independent missing-compiler /
target cases repeat their successful setup rather than sharing mutable state;
there are eighteen host calls across the ten independent C cases.

The sibling [mapped-library suite](../module-library/README.md) replaces
`scripts/check_module_library.py` with 26 IDs. Combined label:
`module-build-library-migration` (**36 registered IDs**).

## Original → CTest map

All names below are prefixed with `integration.module_build.`.

| Stable suffix | Original calls/contracts |
|---|---|
| `numeric` | required descriptor build does not run initializer; exactly one native variant; nonempty compiler name/version; independent actual-library SHA-256; CPU features exactly sse2 / fp+neon / i686; nonempty runtime minimum; ABI 1; every specialization matches exported square functionId; real binary prints 49; extracted library saved; explicit off works without compiler and preserves mainHash and bytecode exactly |
| `empty-required` | no own specialization: normal failure, empty stdout, no output archive, original diagnostic fragment |
| `empty-off` | required descriptor overridden by off with missing compiler; archive has no native part |
| `empty-auto-explicit` | after the same off setup, auto with explicit linux-x86_64-gnu and absent compiler fails |
| `included` | real helper.goat compilation; native specializations exist and refer to file 1 function identities; binary initializer prints 42 |
| `missing-auto` | default auto with absent compiler warns and writes a bytecode-only archive |
| `missing-required` | after auto fallback, required with absent compiler fails and preserves the entire previous archive |
| `unsupported-target` | auto with an unsupported explicit target fails without changing previous archive |
| `foreign-target` | auto never substitutes host for foreign explicit target; previous bytes remain; Linux AArch64 selects Windows x86-64, other hosts select Linux AArch64, exactly as original |
| `only-dependency` | real required-mode dependency compiled and copied into module search layout; imported native code cannot satisfy application's own required specialization; failure has empty stdout and original diagnostic |

`projects.json` was captured from the unchanged original `project()` calls while
all fourteen checks passed. It contains exact Goat source/helper strings and the
exact `json.dumps` descriptor bytes, not production-generated expectations. Native
mode fields are retained as provenance alongside the descriptor string. No compiled
archive/library, target metadata or expected compiler result is frozen.

The runner uses shared module-project/process/file helpers and the independent
ZIP/SHA-256 oracle. It invokes the real `module_import_execution_testing` builder
and the caller's actual C compiler. Normal process exits, source/path identity,
stdout, diagnostics, metadata and bytecode are checked in C, never by a replacement
Python subprocess. Error-fragment checks tolerate Windows stderr CRLF without
normalizing away diagnostic content; binary initializer stdout only normalizes
CRLF pairs. Failed compilations are not accepted on crash, timeout or cleanup error.

## Execution and platforms

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target module_build_scenarios module_library_scenarios --parallel 4
ctest --test-dir build -L '^module-build-library-migration$' --output-on-failure --parallel 4
build/module_build_scenarios integration.module_build.numeric \
  build/module_import_execution_testing /usr/bin/cc build test/data test/data --keep-work
```

`--list` replaces the ID to enumerate this runner's ten cases. Use the actual
MinGW compiler path on Windows; repeat with a Release build. The build runner has
no platform skips on the existing Linux x86-64/AArch64 glibc/musl and Windows
MINGW32/MINGW64/UCRT64 matrix. The library runner has fifteen explicit Windows
skips for ELF/dladdr/dlopen/mprotect-only fixtures, not compiler/runtime failures.

Each independent case gets a unique workspace under the build tree and a shared
360-second deadline, covering at most three original 120-second host calls;
CTest permits 390 seconds for diagnostics/cleanup. Test data remain read-only.
Failed workspaces and `--keep-work` retain sources, descriptors, archives and
command output. The library runner uses 300/330 seconds, covering its longest
original build+two-compiles+loader sequence. Build scripts, explicit Release and
musl target lists **and** test labels include both runners and dependencies; the
obsolete direct musl Python calls are removed.

## Verification and Release dependency

- Original Python: build 14 and library 26 pass Debug/Release (library after the
  test-host fix below), using the same real builder and compiler.
- C: build 10/10 and library 26/26 pass Debug, Release and UBSan. A completely fresh
  Release configuration built only these two targets and their declared
  dependencies and passed 36/36.
- Build exact-input parity: all six source/descriptor recipes were captured from
  the original calls. Direct comparisons of numeric/off, included, missing-auto
  and empty-off match exact source/descriptor bytes and actual metadata/bytecode
  against independent original runs. Numeric also retains and matches the actual
  original native-library bytes; no fake compiler or recorded binary substitutes
  for runtime execution.
- Library parity: every one of the 26 cases in both configurations matches original
  generated C source and metadata bytes, and the original Python `check()` accepts
  the actual C-produced library+manifest under its original expected result.
- Twelve build semantic negatives reject initializer execution, missing compiler
  provenance, wrong native SHA/function identity, changed off-mode bytecode/hash,
  wrong included-file identity, required-mode false success, foreign-failure
  overwrite, omitted own-specialization diagnostic, dependency initializer output,
  and omitted warning. The library suite adds five semantic negatives; see its map.
  All controls reject at intended assertions in Debug/Release, not setup failures.

- Final combined reruns pass **36/36 Debug, Release, UBSan and fresh Release**.
  Adjacent writer/validation regressions pass **121/121 Debug and Release**.
  Both source-fixture SHA-256 checks remain unchanged after the runs. All new
  assets were staged before the whitespace check; global style covers 836 C/H
  files. Unrelated full local CTest was not rerun.

Original Release `module_library_testing` failed on `original.so` with
`Invalid module-v1 loader arguments`: metadata setup lived inside an assert
removed by `NDEBUG`. The library **test translation unit only** now undefines
`NDEBUG` before headers, preserving metadata/readability/ownership/leak assertions
against unchanged production Release libraries. This dependency was recorded
before the fix. No production, schema, format golden or compiler behavior changed.

Final exact-head GitHub workflows provide full platform evidence; links belong in
the implementation PR. The two retired runners exported no helpers imported by
other suites. Shared execution/reference helpers remain for their existing users;
no next checklist task is migrated here.
