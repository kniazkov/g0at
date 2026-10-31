# Shared inspection helpers (#337)

`cases.json` contains 96 read-only inputs and expected results recorded by the
unchanged `scripts/inspect_report_test_support.py` on 28 October 2026. There are
30 accepted and 66 rejected inputs. No production validator computes an expected
answer. Normal CTest runs read this corpus and the published schema files; they
do not run Python, generate reports, or modify inputs.

| Original check | CTest scenario / fixture families |
| --- | --- |
| `schema_check`: published report structure, required and extra keys | `helpers.inspect.parity`: `report.*` |
| Coupled metadata/exports/native, success/exitCode | same: `report.module.*`, `report.exit.*` |
| External `$ref` and local JSON pointers | same: `external.metadata`, `report.module.*`, `escaped.pointer`, `ref.sibling` |
| Probe streams, Base64 pattern, unique completed stages | same: `probe.*`, `base64.*` |
| anyOf/oneOf/allOf, if/then/else, types, const/enum | same: `union.*`, `const.*`, `conditional.*`, `oneOf.*`, `allOf.*` |
| propertyNames, additionalProperties, array/string bounds, numeric bounds | same: `propertyNames.*`, `array.*`, `unicode.*`, `number.*`, `scalar.parity.*` |
| `unique_object` in both Python inspection consumers | `helpers.inspect.parsing`: duplicates, escaped duplicates, nested arrays, embedded-NUL keys |
| Negative test of the test itself | `helpers.inspect.reject-mutant`: deliberately accept all invalid reports; runner must fail |

The implementation is `test/support/inspect_report.{h,c}`, in the test-only
`goat_inspect_report_support` target. It uses yyjson for parsing and the independent
`goat_format_oracle` scalar rules, never production module validators. The parser
returns an owned mutable document and rejects duplicate keys recursively. The
schema checker accepts the subset used by the published inspection and referenced
module schemas. Its patterns are deliberately limited to their six known regular
languages (unknown patterns reject rather than silently accept); it is not a
full JSON Schema or general regex implementation. Pattern checks retain Python
`re.search`'s `$` behavior accepting one final newline; scalar string length still
counts that newline. Base64 padding bits/decoded byte limits are semantic checks
outside the Python schema helper, not silently added here.

Python helpers remain unchanged until their ordered consumer migrations:
`check_inspect_probe.py` (#373),
`check_module_book.py` (#392), `check_module_merge.py` (#374), and
`check_module_delivery.py` (#388). No scenario is
removed or renamed in those existing suites.

To independently reproduce every expected result against identical inputs:

```sh
python3 - <<'PY'
import json, pathlib, sys
sys.path.insert(0, 'scripts')
from inspect_report_test_support import schema_check
root = pathlib.Path('docs/module-format-v1')
published = json.loads((root / 'inspect-report.schema.json').read_text())
cases = json.loads(pathlib.Path('test/data/inspect-report/cases.json').read_text())
for case in cases:
    spec = published if case['schema'] is None else case['schema']
    try:
        schema_check(case['value'], spec, spec, root)
        actual = True
    except AssertionError:
        actual = False
    assert actual == case['valid'], case['name']
print(len(cases), 'Python expected results verified')
PY
ctest --test-dir build -R helpers.inspect --output-on-failure
```

The C runner can also be invoked directly:

```sh
build/inspect_report_testing parity "$PWD/build" "$PWD/test/data" \
  "$PWD/test/data/inspect-report/cases.json" \
  "$PWD/docs/module-format-v1/inspect-report.schema.json" \
  "$PWD/docs/module-format-v1"
```

Use `parsing` in place of `parity` for strict parser self-tests. Appending
`accept-invalid` to `parity` deliberately breaks validation and must exit nonzero,
retaining the scenario work directory and mismatch diagnostic.
