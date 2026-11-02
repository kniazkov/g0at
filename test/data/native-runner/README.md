# Native differential runner policy controls (#383)

The 23 independently selectable original contracts from
`test/src/native/checks/runner.py` are implemented in
`test/src/support/native_runner_policy_testing.c`. Their literal byte arrays,
report strings and expected decisions are independent of the implementation.
`mapping.json` maps these plus five additional edge-control IDs; `provenance.json`
records the unchanged original source hash and extraction provenance.

## Actual shared policy, not a duplicate oracle

Both `native_runner_testing` and these selftests link the same
`goat_native_runner_policy` implementation. `nr_output_equal` performs the real
CRLF-only binary comparison; `nr_check_report` parses the real report and checks
native/VM execution evidence; `nr_manifest_validate` handles actual manifest
validation; `nr_run_selected` performs actual ordered selection and failure
aggregation. The continuation control injects a failing `bad` callback followed
by successful `good`, verifies both were visited in manifest order, and checks
2 total / 1 failure. It does not merely run two separate checks or copy the main
loop's implementation into the test.

The existing manifest's logical `checks/runner.py` entry is routed to the C
selftest executable (`all BUILD DATA`), rather than launching Python or recursively
invoking the main native suite. The original Python file remains alongside it
until full remote parity authorizes removal. Other check entries retain their
explicit Python adapter during this migration.

## Original groups expanded into stable IDs

Prefix: `helpers.native_runner.`

- Five output cases: CRLF-equivalent binary data with embedded NUL accepted;
  missing NUL, missing final newline, extra space and extra empty line rejected.
- Seventeen report cases: valid required report; eight individual mutations;
  each of six required fields missing; valid VM-only report; VM report claiming
  native attempts/success rejected. Counters remain exact strings, not loosely
  parsed numeric equality. Bound positivity is distinct from optional exact bound.
- One continuation case: one failure cannot prevent later cases running.

Five additional IDs cover strict report UTF-8, Python Unicode line separators,
Unicode decimal digits/sign/underscores in optional-bound integer parsing, exact
counter text, duplicate report fields (last wins), first-equals splitting, blank
lines, manifest type/unknown fields/name duplication/missing files, duplicate JSON
keys in both orders, manifest-order selection with deduplicated selectors, and
unknown selector rejection before any callback. A test-only decimal-zero inventory
comes from Python Unicode data, never the production Unicode implementation.
The decimal inventory is pinned to Unicode 15.0.0 (see provenance); exotic
digits added after an older Python runtime's Unicode version are not a promise of
identical acceptance on that older runtime. Actual runner reports use ASCII.
The report integer parser retains Python's default 4300-decimal-digit limit;
non-default Python `int` limit overrides are not adopted by the C runner.

All 28 IDs use ordinary scenario contexts, deferred cleanup and `--keep-work`.
Individual selftests have a 10-second guard; the CTest outer watchdog is 45 seconds.
`all BUILD DATA [--keep-work]` runs every ID, aggregates failure, and still visits
later IDs. Only controlled callbacks are used for policy-level continuation; no
subprocess or native production cleanup behavior is mocked by these pure controls.

## Reproduction

```
cmake --build BUILD --target native_runner_policy_testing
ctest --test-dir BUILD -R '^helpers.native_runner\.' --output-on-failure
BUILD/native_runner_policy_testing all BUILD test/data
```

Full native-corpus execution and platform parity are provided by the main runner;
policy tests alone are not claimed as native functional-suite coverage.

## Local evidence

- Debug, Release and UBSan: **28/28** selftest IDs each.
- Unchanged `test/src/native/checks/runner.py`: all three original unittest methods
  pass (their subtests are expanded into the 23 original C IDs).
- **Eight independent mutation controls** pass across Debug and Release: temporary
  copies of the actual shared policy are changed to accept every output, every
  report, or every manifest, or to abort continuation after the first failed case.
  The unchanged selftest executable rejects each mutant. Repository policy sources
  are not mutated by these controls, and baselines pass before mutation testing.
- clang-format 18 and `git diff --check` pass for the shared policy and selftests.

Workspace evidence is `/tmp/383-policy-tests-{Debug,Release,UBSan}.log`,
`/tmp/383-policy-original.log`, and `/tmp/383-policy-mutants.log`. Full native
functional corpus and remote original/C parity are separate required gates.
