# Test support

Shared test framework components will be added here during the migration.

The data relocation maps `test/{analysis,binary-v1,functional,native,performance,unicode}`
to the same names under `test/data/`. Native Python checks moved from
`test/native/checks/` to `test/src/native/checks/`; native C hosts already live
under `test/src/native/`. Format specifications and their vectors remain in
`docs/module-format-v1/`.

## Execution and diagnostics

`execution.h` adds a test-only fail-fast scope. `scenario_execute` requires a
nonzero overall timeout; the same absolute monotonic deadline is used for every
command, never restarted. Required checks abort the body and invoke registered
resource cleanup callbacks in reverse order. Register ownership immediately;
callback resources must outlive the body stack (for example caller-owned state or
heap allocations), because failure unwinds the body with `longjmp`. Checks remain
active in Release builds.

`scenario_command` saves length-delimited command arguments and exit/status details
in `execution.log`, and complete binary output in `command-N.stdout` and
`command-N.stderr`. `scenario_require_bytes` saves raw expected/actual spans,
including NUL, spaces and empty lines. Its explicit text option normalizes only
CRLF pairs, never whitespace or lone CR. Diagnostic write/close failures are test
failures. Failed and `--keep-work` workspaces are retained by the existing context.

Use `scenario_supervise` for a separate test host that can crash or hang: the
parent owns the deadline, diagnostics and process tree even if the host cannot
return. In-process `scenario_execute` bodies are cooperative and must reach API
checkpoints; arbitrary C loops and cleanup callbacks cannot be preempted safely
in-process. Callbacks must return promptly. This is lifecycle isolation, not a
security sandbox. CTest's outer timeout must exceed the internal scenario budget
by a cleanup margin; framework self-tests allow 30 seconds for budgets up to
5 seconds. Independent CTest cases continue after a failure by default.

Run `ctest --test-dir build -L scenario-execution --output-on-failure` for framework
self-tests. Each deliberate failure is observed by an independent outer test,
which checks the failure result, retained artifacts and skipped subsequent steps.
No existing Python test is removed by this framework addition.

## Independent integer arithmetic (#334)

`integer_oracle.h` / `integer_oracle.c` provide exact sign/magnitude results in
four little-endian 32-bit limbs, then explicit int64 saturation. The test-only
`goat_integer_oracle` target has no production dependency and is never linked
into the interpreter. There is no `__int128` requirement or signed overflow.
See `test/data/integer-oracle/README.md` for Python provenance, reproduction,
scenario mapping and deliberate-corruption self-tests. The existing
`test/src/native/checks/saturation.py` consumer is unchanged until #386;
this step supplies its future independent C oracle, not a replacement runner.

Run `ctest --test-dir build -L integer-oracle --output-on-failure` after building
`integer_oracle_testing` (and `integer_oracle_ubsan_testing` on Unix GCC/Clang).
The same label runs in Debug/Release on Linux x86-64/AArch64 and Windows
MinGW i686/x86-64/UCRT. UBSan instruments the oracle and caller in the glibc Linux CI jobs;
the target is opt-in and excluded from default/package builds because Alpine
musl does not ship the UBSan runtime in its build toolchain.
Windows uses the same portable uninstrumented tests, since MinGW's configured
CI toolchains do not provide the Unix UBSan runtime.

## Independent JSON format oracle (#335)

`format_json.h` / `format_json.c` implement strict parsing, canonical bytes and
schema-subset validation using the shared declarative
`docs/module-format-v1/schema-source.json`, not generated production tables.
`format_analysis.h` / `format_analysis.c` implement scalar/facts normalization and
summary consistency. `format_metadata.h` / `format_metadata.c` implement metadata,
native guards, hash projection and the length-delimited main-hash input.
These files form the test-only `goat_format_oracle` target. Its sole parser/DOM
substrate is yyjson; it has no dependency on `goat_module`, `goat_validation` or
`core`. No production canonicalizer or validator computes expected results.

The new runner `format_oracle_testing` selects individual scenarios through
`ctest --test-dir build -L format-oracle --output-on-failure`. It only reads frozen
inputs and schemas; normal tests never regenerate vectors or source files.
The existing Python reference remains unchanged for the binary migration (#336)
and later consumers. `docs/module-format-v1/test_reference.py` still runs in CI.
The old-to-new scenario map and corpus provenance are in
`test/data/format-oracle/README.md`.

Remaining Python import consumers (including dynamic imports) are:
`docs/module-format-v1/test_reference.py` and `scripts/check_bytecode_v1.py`,
`check_module_abi.py`, `check_module_canonical.py`, `check_module_consistency.py`,
`check_module_container.py`, `check_module_delivery.py`,
`check_module_dependencies.py`, `check_module_graph.py`, `check_module_hash.py`,
`check_module_import_execution.py`, `check_module_inspect.py`,
`check_module_loading.py`, `check_module_memory.py`, `check_module_merge.py`,
`check_module_musl.py`, `check_module_native.py`, `check_module_search.py`,
`check_module_snapshot.py`, `check_module_validation.py`,
`check_module_writer.py`, and `check_inspect_probe.py`. They retain their current
API and data paths; their migration remains in the ordered checklist.

## Independent binary and fixture oracle (#336)

`format_binary.h` / `.c` add independent bytecode construction/validation,
JSON/binary consistency and SHA-256. `format_zip.h` / `.c` construct ZIP records
without production writers, preserving caller order and duplicate entries, and
expose inspection, extraction and bounded byte patching for negative fixtures.
`format_fixtures.h` / `.c` append real external declaration blocks without moving
existing instruction indices. These are part of test-only `goat_format_oracle`;
its extra dependency is the bundled zlib primitive library, not Goat wrappers.
See `test/data/binary-oracle/README.md` for the old/new scenario map, provenance,
corpus hashes, remaining Python fixture consumers and reproduction commands.
The `format-binary` CTest label selects the new tests, also included in
`format-oracle` for the full existing cross-platform CI matrix.
