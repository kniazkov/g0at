# Testing analysis decisions

[Contents](index.md) · [Русский](../ru/testing.md) · [Branch analysis](static-analysis.md)

## What is tested

The [analysis runner](https://github.com/kniazkov/g0at/blob/master/test/src/analysis/analysis_testing.c) parses Goat
source and calls analyze() with an observation collector. It checks decisions
made by the analyzer, without executing the resulting bytecode. For this pilot,
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
