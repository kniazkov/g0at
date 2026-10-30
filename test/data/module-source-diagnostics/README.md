# Compiled source diagnostic scenarios (#363)

The data in `scenarios.json` preserves the exact sources and expected stdout and
stderr fragments from `scripts/check_module_source_diagnostics.py` at baseline
`64247492bb900c31b49ca784736b126795eb6264`. The C runner is
`test/src/functional/module_source_diagnostics_scenarios.c`; its executable is
`module_source_diagnostics_scenarios`. Compilation and execution still use the
real `module_import_execution_testing` host. JSON contains data, not Python code.

## Mapping and preserved checks

All IDs below have prefix `integration.module_source_diagnostics.`.

| Original project(s) | ID suffix | Assertions retained |
| --- | --- | --- |
| `external-local` | `external-local` | Four ordered metadata file identities, internal math label/2:5, no external label; private comment and root absent from image |
| `external-first` | `external-first` | Same mapping; external file #2/3:5 despite identical basenames |
| `external-second` | `external-second` | Same mapping; external file #3/3:5 despite identical basenames |
| `relocation`, `relocated` | `relocation` | Copy actual descriptor/sources to different project root; complete metadata semantic equality, exact bytecode equality, exact normalized stderr equality |
| `imported` | `imported` | Library and application coordinates/context, library frame before application frame |
| `anonymous` | `anonymous` | Original closure coordinates and anonymous/function name pattern |
| `saved-failure` | `saved-failure` | Initializer runs once, `once\ncaught\n`, original include site survives GC/rethrow, later line 4 absent |
| `unicode` | `unicode` | UTF-8 Cyrillic/emoji filename and exact 2:5 location |
| `escaped-label` | `escaped-label` | Literal backslash/newline/ESC/BS/DEL/U+0085 filename, exact escaped label, no slash normalization or unescaped C0/C1 characters |
| `frozen` | `frozen` | READY handshake, delete complete project before release, nonzero VM result, no stdout after READY, dependency/application source coordinates retained |

Every diagnostic must be strict UTF-8, include `Uncaught:`, contain its expected
fragments, and exclude both native and slash-normalized absolute project paths.
External bytecode excludes the source-private comment and project root encoded
as UTF-8 **and UTF-32LE**. The ZIP oracle independently extracts metadata/image;
expected file IDs and flags are not obtained from the production loader.

The unusual external includes remain `../../outside-a/math.goat` and
`../../outside-b/math.goat`; their files are placed only under the owned project
root. The shared module helper's path-validation rules are not weakened.
On Windows, `escaped-label` returns explicit SKIP (77): literal backslash and
control characters are not valid Windows filenames. The other nine cases run on
Windows. Unix runs all ten. CRLF is normalized only when comparing diagnostic
text; bytecode remains byte-exact. Frozen READY uses the host's native newline.

## Reproduction

```sh
cmake -S . -B build/Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/Debug --target module_source_diagnostics_scenarios
ctest --test-dir build/Debug -L module-source-diagnostics-migration --output-on-failure
build/Debug/module_source_diagnostics_scenarios \
  integration.module_source_diagnostics.relocation \
  build/Debug/module_import_execution_testing build/Debug test/data test/data --keep-work
```

Use `--list` instead of the ID to list all ten scenarios. Each invocation owns a
unique directory under the supplied build directory, reads but never edits
`test/data`, has a shared 120-second deadline, and preserves failure logs and
command output. `--keep-work` also retains successful workspaces.

Repeat with `-DCMAKE_BUILD_TYPE=Release`; C assertions use the scenario framework,
not `assert`, so Release retains every check. For UBSan configure with
`-DCMAKE_C_FLAGS='-fsanitize=undefined -fno-sanitize-recover=undefined'` and
`-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=undefined`, then build/run the same target.
The migration label is included in existing Release and musl CI selections;
the ordinary Debug matrix includes all registered scenarios.

To reproduce the old side of parity without restoring a retired runner in-tree:

```sh
git show 64247492bb900c31b49ca784736b126795eb6264:scripts/check_module_source_diagnostics.py > /tmp/source-diagnostics-baseline.py
PYTHONPATH="$PWD/scripts" python3 /tmp/source-diagnostics-baseline.py build/Debug/module_import_execution_testing
```

During migration the original reported **25 host checks** per Debug/Release run;
the C version passed **10/10 IDs with the same 25 host checks** in both builds,
and **10/10 under UBSan**. Windows omits only escaped-label (23 checks, nine runs
plus one explicit skip); remote CI is authoritative for Windows/AArch64/musl.

## Negative controls

Local Debug and Release both reject these intentional semantic mutations (not
setup/process-launch failures), with retained `execution.log` assertion details:

- In a **copied** JSON fixture, change `external-first.mapping[2][1]` to `false`:
  rejects the external metadata flag.
- In a temporary proxy around the real host, change stderr
  `library@2: main.goat:2:5` to `library@2: main.goat:20:50`: rejects coordinates.
- Reverse library/application stderr frame lines without removing any fragment:
  rejects frame order.
- Append the actual binary archive's parent directory to stderr: rejects an
  absolute project-path leak.
- Append UTF-8 U+0085 (`c2 85`) to escaped-label stderr: rejects an unescaped C1
  control scalar, rather than accidentally rejecting arbitrary UTF-8 bytes.

A proxy must pass through compilation unchanged, preserve stdout and exit code,
and modify only the selected binary stderr. Invoke the C runner with that proxy
in place of HOST. Fixture-only controls use a temporary directory containing
`module-source-diagnostics/scenarios.json` as the final FIXTURES argument. Never
modify the canonical read-only fixtures. Each control must return 1 and show the
specific semantic assertion above, not a missing executable/fixture error.
