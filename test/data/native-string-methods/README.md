# Native string methods migration (#385)

The three `.goat` files are the exact bytes produced by the unchanged
`test/src/native/checks/string_method_errors.py` at commit
`4fcd4dbca7a043cede8f23b9b671307cab0b6e7f`. The original `main()` ran against real
Debug and Release Goat; a wrapper around `Path.write_text` copied the completed
files before the original deleted them. No source was extracted/reconstructed
from the AST, and no original assertion was bypassed. `provenance.json` records
the source/helper hashes, fixture sizes/hashes and Windows write counterparts.
The original checker and `scripts/check_native.py` were retained unchanged
through the remote original+C parity gate recorded below, then removed.

## Interface and mapping

Run:

```
native_string_method_scenarios all|ID|--list GOAT REPOSITORY BUILD DATA [--keep-work]
```

DATA is the read-only `test/data` root. REPOSITORY is the original command cwd.
The inherited environment is unchanged except that the final persisted run sets
CC to the case's absolute `missing-compiler` path. Every Goat command uses
`--lang en`, matching the original; CC is inherited unchanged for VM, compilation
and source-native execution.

Each of the three original cases performs the same sequence:

1. VM reference: `--native off --optimize none program.goat`.
2. Compilation: `--compile --native required program.goat`.
3. Source-native: `--native required --save-native native.report program.goat`.
4. Delete `program.goat`, replace CC with the nonexistent per-case path, and run
   `--run --native required --save-native saved.report program.gbin`.

All four phases require zero exit and empty stderr. Both native stdout streams
must equal VM stdout after replacing CRLF pairs with LF, preserving all other
bytes, including NUL, invalid UTF-8, lone CR, spaces and trailing empty lines.
Compile stdout is captured but is not constrained, exactly like the original.
Both reports require these **six exact string values**, not six total fields:

| ID suffix (`integration.native_string_methods.` prefix) | Original case | mode | preparation | bound | attempts | succeeded | retries |
| --- | --- | --- | --- | --- | --- | --- | --- |
| bounds | Valid and invalid substring/charAt/repeat; retry; overflow; empty final substring | required | ready | 3 | 10 | 4 | 6 |
| types | Valid trim, invalid contains argument, missing substring index | required | ready | 1 | 1 | 1 | 0 |
| identity | Method trim versus a shadowing plain callable | required | ready | 1 | 1 | 1 | 0 |
| all | All three original cases, each in its own subdirectory | | | | | | |

The report oracle implements `Path.read_text().splitlines()` and
`dict(line.split('=', 1) ...)`: strict UTF-8 on supported original CI platforms,
CR/CRLF/LF and Python's other Unicode line boundaries, last duplicate key wins,
unknown fields allowed, malformed/blank lines rejected. Values are not trimmed,
parsed as numbers, or normalized. It is independent of Goat's report writer.
The shared test-only `nr_output_equal` implements the original byte comparison.
`nr_check_report` cannot be reused because its different manifest contract
requires attempts==succeeded and retries==0.

## Acceptance controls

| Additional suffix | Required observation |
| --- | --- |
| control-output | Full checker rejects corrupted source-native stdout |
| control-saved-output | Full checker rejects corrupted persisted stdout |
| control-stderr | Full checker rejects nonempty stderr separately in all four phases |
| control-exit | Full checker rejects nonzero exit separately in all four phases |
| control-mode | Full checker rejects wrong mode, separately in native and saved reports |
| control-preparation | Same, preparation |
| control-bound | Same, bound |
| control-attempts | Same, attempts |
| control-succeeded | Same, succeeded |
| control-retries | Same, retries |
| control-report-syntax | Full checker rejects a report line without `=` |
| control-policy | Binary/CRLF stdout acceptance and counterexamples; report splitlines, last-key-wins, unknown fields, exact values, missing fields and invalid UTF-8; full checker accepts these valid edge forms |

Controls re-execute this same runner with an independent controlled command
producer in place of Goat. The baseline must pass first. Corruptions must fail
with their specific diagnostic, so launch errors, timeouts and process cleanup
failures cannot count as successful controls. The producer also requires the
source to exist during the first three phases, creates a distinct binary marker,
then requires the source to be gone, that binary marker to exist, and CC to equal
the exact missing-compiler path during saved execution. Controls do not replace
real Goat parity runs. No test depends on C `assert`, so Release checks stay active.

## Fixtures, diagnostics and lifecycle

Fixtures retain their LF checkout bytes on every platform. The runner applies
exactly the Windows `Path.write_text` LF-to-CRLF expansion when copying them to
`program.goat`, including the leading/trailing newlines. It saves the identical
copy as `program.goat.input` for byte-level parity after deleting the real source.
It never modifies the read-only fixtures or regenerates them during normal tests.

Each registry invocation owns a unique workspace. The `all` workspace contains
`bounds/`, `types/` and `identity/`; independent IDs contain their single case.
Case directories preserve `vm`, `compile`, `native` and `saved` `.command.json`,
`.stdout` and `.stderr` files, plus both reports. Commands also retain framework
`command-N` streams and `execution.log`. Deferred stream aliases preserve original
labels even on timeout or cleanup failure. Command JSON has the original argument
array semantics; JSON whitespace is insignificant. Failures retain work;
`--keep-work` also retains successful runs. A command's 300-second deadline never
extends the shared 300-second scenario deadline. CTest must allow a cleanup
margin outside that deadline.

## Validation recorded before remote parity

On 2026-11-03, the unchanged original passed bounds/types/identity in both Debug
and Release. Every captured input matched the frozen file bytes, and an independent
review verified all three fixture hashes and the unchanged source/helper hashes.
The C runner compiled with `-std=c11 -Wall -Wextra -Werror`; clang-format 18.1.8
formatted the source. All 12 controls passed in Debug and Release.

The three real C cases and aggregate `all` each reached a successful VM phase,
then stopped when the native compile child exited zero but shared supervision
reported `cleanup=2`. This executor lacks `/proc/thread-self/children`; the cleanup
failure is not bypassed, weakened or counted as a parity pass. Full real C parity
therefore remains required from remote CI, including Linux, Windows and musl,
before retiring either unchanged Python source.

After the final diagnostics/path edits, the integrated Debug and Release gates
each passed all 12 method controls. All three real cases and the method
original/C gate failed with the unchanged child-exit-zero/supervisor-cleanup=2
limitation. The total strings gate was 25/30 passing in each configuration;
there is no claim of complete local parity. Independent differential testing of
the actual C parser against Python covered 100,000 report inputs without a
mismatch. Remote full parity is mandatory before Python removal.

## Remote parity and retirement

Exact parity head `ac2a69a6db9ccfb196bb5b957d4d48cefbfd1822` passed
[Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41597613910) and
[vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41597613872).
All nine Linux GCC/Clang, Windows MINGW32/MINGW64/UCRT64 and native musl jobs
ran both unchanged original/C aggregate gates and all 28 new C scenarios in
Debug and Release. Each gate passed twice per job; the four Linux jobs also
passed the explicit UBSan original/C gate. All generated-input and output/report
byte comparisons passed, including Windows CRLF expansion. No retry was needed.

After that full gate, both original Python checkers, their last imported
`scripts/check_native.py` compatibility helper, and the temporary parity wrapper
were removed together with checklist item 67's completion mark. Original scripts
remain available at the parity head for independent provenance reproduction.
The permanent CTest IDs, manifest adapters, sanitizer invocation and complete CI
coverage remain; the final-removal commit must pass its own full CI separately.
