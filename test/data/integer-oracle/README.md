# Exact arithmetic oracle certification (#334)

`python-results.tsv` certifies the independent C test oracle against Python's
arbitrary-precision integer arithmetic. It is **not** used by production or to
replace runtime expected-answer computation in future migrated saturation tests.
The original saturation Python consumer remains unchanged until #386.

The 1,196 input pairs are, in order, the Cartesian product of the 14 boundary
values in `test/src/native/checks/saturation.py`, then all 1,000 frozen pairs in
`test/data/python-samples/saturation-pairs.json` (original seed 20261006).
Every pair has seven records: add, subtract, multiply, negate, absolute value,
increment, decrement. Unary operations ignore the second operand. There are
8,372 records, with no header, ASCII decimal fields separated by tabs and LF
line endings. Each record has `operation a b negative limb0 limb1 limb2 limb3
saturated`, with operation IDs 0–6 in the order above and little-endian 32-bit
unsigned magnitude limbs. Zero has a nonnegative sign. Neither Python nor C uses
production arithmetic helpers to compute these expectations.

SHA-256:

- Input `saturation-pairs.json`: `a2233cd9bf883b381b22deff87447d5b04c70b6c58653c282b67c2191f394461`
- Output `python-results.tsv`: `49881187e2507b5dc5010fb37e2519d54b5501f34f04bed886f27663e4e6ae2d`

## Scenario mapping

| Original / added contract | CTest ID |
|---|---|
| Python exact integers before/after clamp: all boundary and frozen pairs, all seven operations | `framework.integer-oracle.parity` |
| Checker rejects altered magnitude, rotating across all four limbs | `framework.integer-oracle.reject-value` |
| Checker rejects altered sign, including zero | `framework.integer-oracle.reject-sign` |
| Checker rejects altered saturated result | `framework.integer-oracle.reject-clamp` |
| Exact parity and clamp boundaries under undefined-behavior sanitizer (supported compilers) | `framework.integer-oracle.ubsan` |

All scenarios first verify the original results and 13 explicit full-width clamp
boundaries (including either sign of zero, INT64_MIN/MAX, 2^63+1, 2^64, 2^96,
and 2^128−1). The three negative scenarios deliberately corrupt one result field
per record and require the same comparison routine to reject it. Checks do not
use `assert`, so Release/NDEBUG runs retain them. Malformed, truncated, extra,
unreadable fixtures and unknown scenarios return failure. Runs only read inputs.

Build/run from the repository root:

```sh
cmake -S . -B build/oracle -DCMAKE_BUILD_TYPE=Debug
cmake --build build/oracle --target integer_oracle_testing
# Unix GCC/Clang sanitizer target (not available on MinGW):
cmake --build build/oracle --target integer_oracle_ubsan_testing
ctest --test-dir build/oracle -L integer-oracle --output-on-failure
```

## Explicit Python reproduction

Run this **only when intentionally reproducing the certification data**, from
the repository root. Normal build and test runs do not execute Python or rewrite
the fixture. Compare the resulting bytes/hash to the committed fixture:

```sh
python3 - <<'PY'
import hashlib
import itertools
import json
from pathlib import Path

root = Path('.')
source = root / 'test/data/python-samples/saturation-pairs.json'
minimum, maximum = -(1 << 63), (1 << 63) - 1
edges = [minimum, minimum + 1, minimum + 2, -3037000500, -2, -1, 0,
         1, 2, 3037000499, 3037000500, maximum - 2, maximum - 1, maximum]
frozen = json.loads(source.read_text(encoding='utf-8'))
assert len(frozen) == 1000
pairs = list(itertools.product(edges, repeat=2)) + frozen
rows = []
for a, b in pairs:
    for op, value in enumerate([a + b, a - b, a * b, -a, abs(a), a + 1, a - 1]):
        magnitude = abs(value)
        limbs = [(magnitude >> (32 * i)) & 0xffffffff for i in range(4)]
        assert magnitude < 1 << 128
        fields = [op, a, b, int(value < 0), *limbs,
                  min(maximum, max(minimum, value))]
        rows.append('\t'.join(map(str, fields)))
output = root / 'test/data/integer-oracle/python-results.tsv'
output.write_bytes(('\n'.join(rows) + '\n').encode('ascii'))
print(len(rows), hashlib.sha256(source.read_bytes()).hexdigest(),
      hashlib.sha256(output.read_bytes()).hexdigest())
PY
```
