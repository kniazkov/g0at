# Launch-bound context constants (#360)

Copyright 2026 Ivan Kniazkov. Distributed under the repository MIT license.

`sources.json` preserves all 16 fixed source/expected strings from
`scripts/check_module_context_constants.py` at commit
`d40c232fe471af61fdf39424c2d2b192319f6dbb`. Values were extracted from the Python
AST, not recomputed using the implementation under test. The fake compiler is
[`context_retarget_compiler.c`](../../src/support/context_retarget_compiler.c):
the former embedded compiler body, with formatting and file documentation only.

[`module_context_scenarios.c`](../../src/functional/module_context_scenarios.c)
replaces the Python runner with 36 independently selectable scenarios. The
original canonical fixture is split into direct/alias and ordinary/frozen
variants, with isolated equivalent setup. Reserved names have one ID per invalid
form. Together with the 13 [provenance scenarios](../module-provenance/README.md),
this completes the scope of #360. No other consumers import the two removed
runners; the shared Python execution helper remains for later tasks (#394).

## Preserved checks and infrastructure

- Identical library bytes work both directly and imported; imported `__main__`
  is false and `__dir__` belongs to the launch context. The library bytes remain
  unchanged after execution.
- File/module identity survives callbacks, nested closures, method storage,
  cloning/flattening, function cloning and reexport chains.
- Every extracted archive entry is searched for the build root in both native
  and slash path spellings, encoded as UTF-8 and independently encoded UTF-32LE.
  The unchanged archive is renamed and the original source tree removed before
  testing relocation. The ZIP oracle is independent of production ZIP wrappers.
- UTF-8 launch paths include Cyrillic, Greek and a supplementary-plane owl.
- Alias-side corrupt dependencies cannot replace dependencies beside the real
  archive. A READY barrier freezes the launch before cwd change/alias retarget.
  The raw barrier uses the host's platform newline; final stdout is normalized.
- CLI source paths (direct and symlink) run `none/off`, `all/off`, `all/auto`.
  Native preparation calls an actual compiled stand-in that retargets the link
  and fails, requiring ordinary VM fallback. The marker, final link kind/target,
  launch directory and output are all checked.
- Reserved-name assignments/declarations, unreachable branches, parameters,
  catch and loop variables must fail compilation normally, emit diagnostics,
  and leave no artifact. Release does not disable any check.
- All work is scenario-owned under the build directory with a 120-second shared
  deadline and CTest's 150-second cleanup allowance. Commands/streams/inputs are
  retained on failure, removed on success, or retained with `--keep-work`.
  Source fixtures are read-only; cwd and child environment are explicit.
- Windows without symlink privilege returns CTest SKIP (77) for only the four
  alias scenarios, with an explicit message. Other link errors are failures;
  direct-path counterparts still run. Linux/musl never silently skip links.

## Reproduce

```sh
cmake -S . -B build/context -DCMAKE_BUILD_TYPE=Debug -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE
cmake --build build/context --parallel 4 --target module_context_scenarios module_function_provenance_scenarios
ctest --test-dir build/context --output-on-failure --parallel 4 -L module-context-provenance-migration
build/context/module_context_scenarios integration.module_context_constants.frozen-alias build/context/module_import_execution_testing build/context/goat build/context test/data test/src/support/context_retarget_compiler.c --keep-work
# Replace the ID by --list for the complete context registry.
```

Repeat using Release and a separate UBSan configuration with
`-DCMAKE_C_FLAGS='-fsanitize=undefined -fno-sanitize-recover=all'`.
The same label is integrated into Linux/Windows Release and musl runs.

## Parity and negative checks

The original Python context runner passes Debug and Release with 53 checks.
The C scenarios use the same source bytes; an independent AST comparison also
verified every fixed string and all 24 expanded reserved forms. Provenance has
its own executed-input parity record. C expected path values are derived from
the actual canonical launch location, preserving the Python `Path.resolve`
contract, not from emitted program output.

Local C runs exercised all 49 combined IDs in Debug, Release and UBSan. This
container lacks `/proc/thread-self/children`: the shared Linux supervisor can
report cleanup ENOENT after `native-alias` despite exit zero and correct stdout.
This environmental failure is retained, not ignored or converted to SKIP; the
normal-runner CI is required before completion. All other cases pass; complete
49/49 runs were also observed in Debug and UBSan. See the PR for final CI results.

Negative copies reject wrong expected stdout, a deliberately injected UTF-8
build-path ZIP entry, and a UTF-32LE path entry. The ZIP negatives fail the
specific absence assertions before VM execution, with all diagnostics retained.
Provenance additionally rejects unexpected successful compilation and malformed
JSON. No production API or cleanup check is weakened for the migration.

To reproduce Python, extract the original runner from the provenance commit
into a build directory, use the current `scripts/` as `PYTHONPATH`, and run
`python3 check_module_context_constants.py HOST GOAT`. Ordinary CTest runs need
neither Python nor fixture regeneration.

## Exact scenario mapping

All IDs below are prefixed by `integration.module_context_constants.`.

| Original block/check | CTest suffix |
|---|---|
| Same image direct/imported launch and unchanged bytes | `same-image` |
| File origin, callbacks, closures, methods and clone/flat | `file-origin` |
| Library/facade reexport identity and launch directory | `reexport` |
| Every ZIP entry path absence, rename, source removal, moved launch | `relocation` |
| Unicode compiled archive launch | `unicode` |
| Real archive/dependency directory | `canonical-direct` |
| Archive alias with corrupt alias-side dependency | `canonical-alias` |
| Frozen real archive plus child cwd change | `frozen-direct` |
| Frozen alias, retarget to wrong archive, child cwd change | `frozen-alias` |
| Unicode source, all three optimizer/native pairs | `source-direct` |
| Unicode source alias, all three optimizer/native pairs | `source-alias` |
| Fake compiler retarget, VM fallback, marker and link checks | `native-alias` |

For each row below there are two IDs, `reserved-main-N` and `reserved-dir-N`,
substituting `__main__` and `__dir__` for NAME. Each independently checks compile
rejection, diagnostic presence and missing artifact.

| N | Original invalid form |
|---|---|
| 0 | `NAME = 1;` |
| 1 | `NAME += 1;` |
| 2 | `var NAME = 1;` |
| 3 | `const NAME = 1;` |
| 4 | `public NAME = 1;` |
| 5 | `external NAME = 1;` |
| 6 | Local `var NAME` in a function |
| 7 | Function parameter NAME |
| 8 | Assignment to NAME in a function |
| 9 | `const NAME` in an unreachable branch |
| 10 | Catch parameter NAME |
| 11 | Loop variable NAME |
