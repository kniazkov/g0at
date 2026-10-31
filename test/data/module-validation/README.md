# Shared module validation migration (#366)

`test/src/functional/module_validation_scenarios.c` replaces
`scripts/check_module_validation.py` from baseline
`e95f826f8aaa0e6f557dbdd6618e06e2efe4e4e2` without production/schema/vector changes.
There are **62 stable CTest IDs**, label `module-validation-migration`:
53 ordinary archive cases, four real file cases, four constructor/role cases,
and the component API assertions. [Complete mapping](cases.md).

## Contracts and provenance

`scenarios.json` contains the exact original Python archive/input bytes, the
handwritten expected status/limits/metadata-presence parameter, and the diagnostic
JSON/results observed while **all original Python assertions passed**. These are
new test fixtures, not format golden vectors. ZIP timestamps are frozen, not
compared with the current clock. Deliberately malformed ZIP/CRC/JSON/semantic
inputs are sent unchanged to the real validation host; no independent prevalidator
can reject or repair the intended corruption before the production call.

The C runner separately checks every original universal assertion: normal exit,
status, event/failure/skipped counts, exact archive label, metadata presence and
`validation` name, and successful bytecode/native phases without failures/skips.
It compares the complete ordered diagnostic objects (including simultaneous
failures, schema field paths, skipped prerequisites and container prerequisites)
and complete metadata against the recorded fixture. Thus the original selective
predicates are preserved, with stronger coverage of previously unconstrained
fields. It repeats each ordinary case and explicitly compares diagnostics, rather
than replacing the original repeated-valid/schema assertions with unrelated runs.

File cases use a real UTF-8 `модуль.gbin` path, the original archive bytes and
limits. The component retains its input-mutation/path-replacement snapshot
assertions; C additionally verifies exact `replacement` bytes, unchanged input on
limit failure, and true `FS_NOT_FOUND` for the missing path.

Each constructor role independently compiles the **same real C sources** as the
original, positively loads the library through `dlopen`/`LoadLibraryA`, verifies
`loaded`, removes the marker, and then validates good and wrong-mainHash archives.
The marker must remain truly absent (a directory or lookup error also fails).
The independent test oracle computes actual library SHA-256/mainHash and builds
stored ZIPs with the actual library payload; no compiled output is frozen.
`good.gbin`/`bad.gbin` are retained with `--keep-work` or failures for reproduction.
The original four roles compiler/loader/inspect/merge remain separate IDs.

## Platforms, resources and Release

Linux x86-64/AArch64, glibc/musl and Windows MinGW i686/x86-64/UCRT use the same
cases, with **no skips**. Unix compiles `probe.so` using `-shared -fPIC` and links
loader `-ldl`; Windows uses `probe.dll`, `load.exe`, `LoadLibraryA`, no Unix flags,
and the original Windows/ucrt metadata target override. `CC` retains POSIX
`shlex.split` quoting semantics on both platforms, with defaults `cc`/`gcc`.
The subprocess API handles Unicode arguments and binary stdin. JSON parsing
accepts CRLF; output comparisons only normalize CRLF pairs.

One necessary **test-only dependency** was exposed: `module_validation_testing.c`
put metadata serialization and snapshot/API checks inside `assert`. The original
built Release test failed immediately with successful status but `metadata:null`.
`NDEBUG` is now undefined before all headers in this test translation unit only;
production Release libraries and assertions are unchanged.

Each ID gets an isolated scenario workspace under build, read-only test/data,
shared monotonic deadline, full process/output diagnostics and `--keep-work`.
Ordinary cases allow 60 seconds (two former 30-second calls); API/file/constructor
cases allow 180 seconds, with a 210-second outer CTest timeout. Child cleanup
errors are failures, never skips or ignored results. No reference helper is used
to generate expected results at runtime.

## Reproduction

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target module_validation_scenarios --parallel 4
ctest --test-dir build -L '^module-validation-migration$' --output-on-failure --parallel 4
build/module_validation_scenarios integration.module_validation.constructor.compiler \
  build/module_validation_testing build test/data test/data \
  docs/module-format-v1/schema-source.json --keep-work
# Substitute --list for the ID to enumerate all 62 selections.
```

Repeat with a Release build. Unix/MinGW build entry points include the new target;
Release and musl CI filters include the label. The ordinary corpus cannot write
into the checkout. Mutated fixtures for negative controls belong in a temporary
copy passed as `FIXTURES`, not in tracked test/data.

## Local verification before retirement

- Original Python: **65 validation calls + API smoke**, Debug and Release after
  the test-host assertion fix. Built original Release failure was recorded first.
- C: **62/62 Debug, Release and UBSan**; actual constructor compilations and loads
  are included, with no local cleanup exception or skipped producer.
- Same-input replay: all **53 exact frozen input byte sequences** passed the
  original `run()` predicates and matched its diagnostic objects in Debug/Release.
  The C-generated good/bad constructor archives were fed to original Python
  predicates for all four roles (8 calls per configuration). Their metadata,
  bytecode and actual library payload matched Python's independent archive builder
  (ZIP timestamp/JSON whitespace are not semantic differences).
- Ten semantic negative controls in both configurations rejected: wrong status,
  phase, skipped prerequisite, omitted second schema error, metadata name,
  diagnostic ordering, damaged expected-good input, reported failure count,
  wrong file replacement, and a constructor marker recreated as a directory.
  Each reached its intended assertion, not an unrelated setup failure.
- Malformed quoted `CC` was rejected; a quoted compiler path containing spaces
  plus a separately quoted compiler argument passed the real constructor case.
- Adjacent container and inspect regression: both pass Debug; container passes
  Release CTest, whose Python inspect registration is disabled in the reused
  configuration. The original inspect runner also passes when invoked directly
  against the Release executable (133 checks).
- Style (all tracked C/H plus the new runner) and `git diff --check` passed.

Full unrelated local CTest was not rerun: existing ACL/child-cleanup environment
limitations are outside this migration. Exact-head GitHub workflows remain the
cross-platform authority; PR/green-run links are recorded in the implementation PR.

The retired script exported no imported helpers. `module_interface_fixtures.py`
is retained for `check_module_merge.py`;
`reference.py` remains for its other consumers. Final cleanup is tracked by #394.
