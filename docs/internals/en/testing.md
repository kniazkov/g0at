# Testing the implementation

[Contents](index.md) · [Русский](../ru/testing.md) · [Branch analysis](static-analysis.md)

## What is tested

The [analysis runner](https://github.com/kniazkov/g0at/blob/master/test/src/analysis/analysis_testing.c) parses Goat
source and calls analyze() with an observation collector. It checks decisions
made by the analyzer, without executing the resulting bytecode. For the branch examples,
the observable fact is an `ANALYSIS_UNREACHABLE` event at a skipped subtree root.
A program printing the expected text would not establish that this proof exists.

The input and oracle are separate files in `test/data/analysis/`:

- `<name>.goat` is the source program.
- `<name>.expect` contains manually specified assertions about analysis events.
- `list.txt` selects which pairs the runner executes.

Doxygen embeds these files directly. The documentation check also requires each
embedded source to have an expectation file and an entry in `list.txt`. The
normal Debug core tests execute the analysis runner on Linux and Windows;
CTest exposes the same suite as `core.analysis`.

## Reading a reachability expectation

Take the expectations for [dead_known_true](https://github.com/kniazkov/g0at/blob/master/test/data/analysis/dead_known_true.expect):

<!--! \verbinclude dead_known_true.expect -->

There are six fields: match mode, event kind, source row, declaration row,
variable selector and expected value. For `unreachable`, the last three fields
must be `0 - -` because this event has no declaration or value payload.

`one unreachable 3 0 - -` requires exactly one event on source line 3.
`none unreachable 2 0 - -` forbids that event on line 2. A source row of zero is
a wildcard, so the unknown-condition fixture forbids unreachable events anywhere.
The general runner also supports `last`, which requires at least one match and
checks the latest value; reachability examples use `one` and `none`.

The expected answer is not regenerated from the current analyzer. Otherwise an
incorrect implementation could rewrite its own oracle and appear correct.
Checking both the dead branch and the branch that must remain live catches
mistakes that a positive assertion alone can miss.

## Running and diagnosing

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4 --target analysis_testing
ctest --test-dir build --output-on-failure -R '^core[.]analysis$'
```

Or call `build/analysis_testing test/data/analysis` directly (on Windows the
executable is `build/analysis_testing.exe`). This runs the whole analysis list,
including all examples used in the branch guide.

A mismatch reports the `.expect` filename, expectation line, selector and match
count, then prints the observed analysis events. Start with the source location:
an unreachable event belongs to the skipped subtree's root, not each child.
When editing a fixture, review line selectors along with the intended semantics.
Do not update expectations merely to silence a failure.

run_case() retains source, AST and collector until assertions and diagnostics
finish, then releases them and checks allocation balance. An empty or malformed
expectation set fails, as does an empty or invalid test list.

These checks verify the explicitly stated analysis properties. They do not
prove arbitrary explanatory prose, bytecode behavior or all cases of a rule;
review and the separate execution suites remain necessary.


## The rest of the test system

| Layer | Source and data | What it establishes |
| --- | --- | --- |
| Unit | `test/src/unit` | Local algorithms, model/VM and ownership contracts |
| Component | `test/src/component` | A real subsystem through a controlled host |
| Analysis | `test/src/analysis`, `test/data/analysis` | Explicit facts, effects and eligibility events |
| Functional | `test/src/functional`, `test/data/functional` | Observable language results |
| Scenarios | `test/src` runners, `test/support`, `test/data/<family>` | Processes, files, native libraries and end-to-end transactions |
| Format / generators | Independent helpers and committed vectors/schema/UCD inputs | Byte contracts, malformed inputs and reproducibility |

Inspect `test/CTestLists.cmake` for the actual registrations and labels. A CTest
case can contain many assertions; a count of CTest cases is not a count of all
checks. Likewise, a SKIP is not a PASS. Platform-specific unavailability must be
reported explicitly rather than silently replacing a real check with success.

## Scenario lifecycle and independent oracles

scenario_create() makes an exclusive work directory below the build tree, never
modifies fixture data, and retains failures or explicitly requested workspaces.
scenario_execute() gives the body one monotonic deadline, captures child commands
and binary output, and runs deferred cleanup in reverse order. Its body is
cooperative: arbitrary crashes and hangs belong in a supervised child. Inner
substeps cannot extend the outer deadline. scenario_finish() can turn an otherwise
successful body into failure if owned resources cannot be cleaned up.

`test/support/format_*` supplies independent JSON, ZIP, binary and hash oracles;
`process_watch` observes pinned OS identities without the production supervisor.
This independence prevents a bug shared by writer and reader from certifying itself.
Negative controls deliberately corrupt input or the expected result and must fail.
Frozen samples have documented provenance; do not regenerate an expected answer
from the implementation under test simply to make a comparison pass.

Retained diagnostics should answer: which command, which exit state, what stdout/
stderr bytes, which assertion, and which cleanup operation failed? Start there before
rerunning a flaky case. Wrong output, timeout, crash and cleanup failure need different
fixes. Output comparison policy is helper-specific: the module execution helper
preserves byte lengths and normalizes CRLF, not arbitrary whitespace.

## Local selection and CI

From the repository root, for a single-configuration Debug build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build -N
ctest --test-dir build --output-on-failure -L core
ctest --test-dir build --output-on-failure -R '^integration[.]module_delivery[.]execution$'
bash scripts/check_docs.sh
```

Use `-C Debug` for multi-configuration generators. Choose `-R` for a case name or
`-L` for a label; inspect `-N` first rather than assuming every platform exposes
the same cases. Build the targets required by the selected suite. The scenario
registration helper permits parallel isolated cases, honors resource locks and
serializes performance cases; legacy suite registrations remain serial.

The checked-in build scripts support separate build/test phases used by CI.
The workflows are the authority for compiler/platform combinations and artifacts;
local Linux success cannot establish Windows loader or process-handle behavior.
Documentation changes need Doxygen and relevant factual examples checked locally;
CI additionally catches platform compilation and integration regressions. Review
the browsable Doxygen artifact for presentation, not only the job's exit code.

## Adding a useful test

Choose the lowest layer that observes the promised property, supply a reviewed
oracle, and add the test to its normal manifest/CTest registration. Test both the
allowed case and a nearby forbidden/unknown case. For an analysis explanation,
assert the event; for a lifetime guarantee, observe the resource; for a native
speed claim, measure already compiled execution. Keep fixture source and docs
linked so line-based selectors are reviewed when the example changes.
