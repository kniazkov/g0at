# Standalone SVG checks (#393)

## Independent original and frozen inputs

The reference is the embedded Python in `scripts/check_graph.sh` at base
`f6b302d1772950794463272e02889e78f9ea0ca9`. The original whole script SHA-256 is
`a601683f0e2479209d36f9660ca6514d9e1dfdc47c17014ec9658d34ce1108f9`.
The exact heredoc, including its final newline, hashes to
`2431003535c1426296b9443abb221269f7f63bce36623348b9909b667581cde7`.
It remains unchanged until complete original/C CI parity passes.

The four frozen files are real output from the unchanged Goat commands, not
handwritten approximations. They were generated with the preceding #392 retired
Debug build (`/tmp/g0at392-retired-debug/goat`) and Graphviz `dot` 2.42.4 (0), using
`example/fibonacci_analysis.goat` and `example/replacements.goat` from this base.
Fontconfig warnings on stderr did not affect the SVG or stdout predicates.
The checkout keeps these files byte-identical on Windows through `.gitattributes`.

| File | SHA-256 |
| --- | --- |
| fibonacci-analysis.svg | 632473d0ab546b17b78c5093f0be53eac6c4d8d7d50c14e138ad6db744c0919d |
| fibonacci-output.txt | 4c82a221b575ce7fe118b2e8cdf0764bf4ef570a3017e80b6d3438af9095f376 |
| replacements-analysis.svg | 264ae34d6d5c81b4d75d961be811d2b03deed8694c30a70ec5377f458d40ebec |
| replacements-output.txt | ac813140110a502a0f946a1922738f97ee0725b187cd3f40798968d10fe7f5fb |

## Scenario correspondence

| Original assertion | C validation in `integration.graph` |
| --- | --- |
| Fibonacci stdout stripped equals `55` | Compare trimmed file text |
| Fibonacci XML parses | Independent test-layer XML reader |
| Joined descendant text includes `C view:`, `(integer)` and `C=supported` | Search decoded text in document order |
| Some node has stroke `forestgreen` | Exact decoded attribute value |
| Some node has fill `#f2faf2` | Exact decoded attribute value |
| Replacement stdout stripped equals `result: 14` | Compare trimmed file text |
| Replacement XML parses | Independent test-layer XML reader |
| Joined text includes `expression replacement`, `statement replacement`, `original`, `replacement`, `if-else` | Search decoded text in document order |
| One node has both fill `#f5efff` and stroke `purple` | Require both attributes on the same element |

`integration.graph_controls` is a separate stable CTest ID for valid variations,
malformed XML and predicate mismatches. Neither scenario relies on C `assert`,
so Release retains validation. The XML reader is test-only and intentionally
limited to the production Graphviz SVG contract: UTF-8 XML 1.0, ASCII XML
names, canonical namespaces and an external doctype without an internal subset.
It is not a general replacement for all ElementTree inputs, an XML API, or a
production change. Parity covers real generated SVG and the documented supported
variations; unsupported constructs fail closed.

## Parity and platform applicability

During parity, `scripts/check_graph_parity.py` extracts and verifies the exact
original heredoc, runs it in an independent Python interpreter with optimization
disabled, then runs C against the same copied fixture quartet. Neither checker
reads the other's verdict. All nine existing platform jobs select this fixture
parity and C controls in Debug and Release, including both native musl jobs.

Actual graph generation retains its original applicability: the Linux GCC
x86_64 graph job installs Graphviz and runs the unchanged two Goat launches in
`scripts/check_graph.sh`. Both original Python and C inspect the same generated
files. Other platforms validate the frozen generated files and controls; they do
not claim actual graph-generation coverage. Shell/CMake remain orchestration.

The original `tool.graph` CTest ID stays optional on Bash, Graphviz and, during
parity only, Python. The portable `graph-validation-migration` tests do not require
Graphviz or Bash. Python is only the temporary original-reference dependency;
C validation/controls can always be run directly.

## Workspaces and reproduction

```sh
cmake --build build --target graph_scenarios
ctest --test-dir build --output-on-failure -L graph-validation-migration
build/graph_scenarios integration.graph build test/data/graph test/data/graph --keep-work
build/graph_scenarios integration.graph_controls build test/data/graph test/data/graph
GOAT_BINARY="$PWD/build/goat" GRAPH_SCENARIOS_BINARY="$PWD/build/graph_scenarios" \
  GRAPH_BUILD_ROOT="$PWD/build" bash scripts/check_graph.sh --keep-work
```

The C runner takes `ID BUILD DATA INPUT [--keep-work]`. DATA is the read-only
fixture directory; INPUT contains the quartet to inspect. BUILD must exist and
be disjoint from DATA. Every run owns a unique build workspace and copies the
actual inputs before validation. Failures and `--keep-work` retain these files;
successful ordinary runs clean them. The parity wrapper independently retains
its copied quartet and exact original reference on failures or `--keep-work`.
Both C scenarios have a 30-second internal budget. CTest watchdogs are 210
seconds for parity and 180 seconds for controls; the
wrapper bounds Python at 30 seconds and C at 180 seconds.

Linux/Windows failure artifacts retain SVG, stdout and parity inputs. The musl
EXIT handler copies the same evidence before the container exits. The actual
generation job uploads SVG and stdout even if validation fails. No fixture or
source input is overwritten by either implementation.

Retirement is pending the complete exact-head 14/14 parity gate. Do not remove
the original heredoc, temporary wrapper or its dependency before that gate.
