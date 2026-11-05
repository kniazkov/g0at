# Book and local-link checks (#392)

The independent reference was `scripts/check_module_book.py` from base
`8b2550a859b6db153e9e57d00256c1d8e9fbb103`.
Its raw SHA-256 was
`2109f474f495ae6c8b997028296ce62cbf8cb76b9381d967d945f49867214ee0`.
The original and temporary parity wrapper remain available in Git at parity head
`17e6de35ce28531cf6b140835916147d9612748a`. The wrapper verified the original
bytes and independently ran both implementations against the same real checkout
and adjacent `goat`, `goat-inspect` and `goat-merge` executables. Neither
implementation consumed the other's results. Original bytes were protected on
Windows, and Python optimization was disabled to preserve its assertions.

## Runtime inputs, not duplicated snippets

`README.md`, `README.ru.md`, both language directories under `docs/book`,
`docs/book/examples/30-modules`, `docs/book/examples/22-native-module.goat`,
`example/hello_world.goat` and the inspect-report schemas are read from the
checkout at runtime. The closure and loop source comes from the marked README
blocks themselves. The tutorials must match all six specified commands in all
four entry points. No golden source copy can hide documentation drift. External
URLs and anchor-only links are not fetched.

## Scenario correspondence

| Original check | C coverage in `integration.module_book` |
| --- | --- |
| RU/EN book page basenames agree | Compare both language page sets |
| Exactly one module tutorial in each README and modules chapter | Parse marked shell block, require all six commands in order |
| Balanced triple-backtick fences | Count line-leading fences before link scanning |
| Existing relative Markdown link destinations | Ignore code blocks/inline code, schemes and anchors; percent-decode destination before existence check |
| One closure and loop block in each README; RU/EN equality | Read marked `text`/`goat` blocks and compare actual source |
| Closure and loop execution | Require `5\n` and `10\n` (commands 1–2) |
| Actual hello example | Require `it works!\n` (command 3) |
| Save native-module example as standalone bytecode | Compile silently, remove source, run saved program and require `7\n6\n4.0\n` (commands 4–5) |
| Geometry/app build, source-app run, saved-app run | Require silent builds and `25\n` runs, all with `--native off` (commands 6–9) |
| Inspect report | Independent schema check, version 1, success, tutorial/geometry module-name set, passed validation, no native payload; empty stderr (command 10) |
| Merge geometry | Metadata equals original; two included files and `square` external; app dependency name/version/hash exactly references geometry (command 11) |
| Ready distribution without sources/descriptors | Remove copied `src` and `.gmod` files, saved app still prints `25\n` (command 12) |
| Missing external geometry dependency | Run fails with empty stdout; inspect fails with unsuccessful JSON and nonzero exitCode (commands 13–14) |
| Merge without resolving external dependencies | App still merges and preserves its metadata after dependency removal (command 15) |

All tool commands retain empty stdin, a cleared `GOAT_MODULE_PATH`, the original
60-second per-command timeout, and original exit/stdout/stderr predicates.
The C scenario budget is 900 seconds for all 15 commands; CTest allows 960
seconds including supervisor/cleanup margin. Its controls have a separate
60-second scenario budget and 180-second CTest watchdog. CRLF
normalization is applied where the Python checker normalizes stdout. Source and
archive metadata checks use independent test-layer parsing, not production
validation to assert itself. Workspaces contain spaces in their paths. Failures
retain C workspace/report evidence under the selected build directory; successful
runs clean up unless `--keep-work` is supplied. Linux/Windows artifact uploads
and the musl EXIT handler retain the complete `module tutorial` tree, including
archives, descriptors and extracted snippets, rather than filtering file suffixes.

## Negative controls

`integration.module_book.controls` first audits the real checkout documents,
then checks the same predicates used by the runtime scenario:

- Changed, missing and duplicate tutorial commands/blocks are rejected.
- Duplicate or unterminated README snippet markers are rejected; changing the
  actual extracted closure source in a copied README fails the equality check.
- An unclosed fence and a broken local destination are rejected; anchor fragments,
  percent-encoded paths, external URLs and links inside code retain their intended
  behavior. Controls also cover Unicode whitespace and line breaks in tutorials,
  nested external-link text, percent-encoded absolute destinations, encoded `#`
  filenames, and malformed UTF-8 escapes decoded with replacement characters.
  All-host Windows path controls cover same-drive and other-drive relative paths,
  drive casing and empty suffixes, rooted local paths, and UNC roots.
- Reordered matching page sets pass; missing pages fail in both directions.
- Wrong, truncated, trailing and NUL-bearing stdout fails exact output matching;
  expected CRLF normalization passes.
- A native payload or failed validation fails the tutorial success predicate.
- A schema-valid empty report fails the tutorial-specific success predicate;
  extra and missing schema fields fail independent schema validation.
- Contradictory failure-report success/exitCode combinations and wrong exitCode
  types fail; the real failing-report shape passes.
- Stale dependency hashes and unequal merged metadata fail.

These are test-layer controls, not production mutations. They require neither
Python nor enabled C assertions, and remain selected in Release. Actual missing
external dependencies are also exercised by runtime commands 13–15 above.

## Parity gate and retirement

Original Python and the temporary wrapper were retired after [PR #473](https://github.com/kniazkov/g0at/pull/473)
passed [Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41915800670) and
[vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41915800547) on exact head
`17e6de35ce28531cf6b140835916147d9612748a`. All nine platform jobs passed actual
Debug/Release original/C parity and controls, including both native musl
architectures and the additional explicit musl Debug invocation.

`integration.module_book` keeps its original CTest name and now directly selects
the C runner. The separate negative-control registration, Linux/Windows Debug
integration selection, Debug/Release migration labels, and explicit musl Debug
CTest invocation are retained. The scenario map, controls and full failure-tree
artifact preservation remain after retirement. The C runner source is unchanged
from the parity gate (SHA-256
`30a59d00f108221a6d43643cee0c60e0965a9859cfbb75b33a4e5e17db2880a8`).

`scripts/inspect_report_test_support.py` had no remaining runtime consumers after
this migration and was subsequently removed in #394. Its historical path is
provenance, not a current dependency. This book migration did not include #393.

## Reproduction

```sh
cmake --build build --target module_book_scenarios
ctest --test-dir build --output-on-failure -L module-book-migration
build/module_book_scenarios integration.module_book \
  build/goat build/goat-inspect build/goat-merge build "$PWD" --keep-work
```
