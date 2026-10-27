# Frozen pre-migration Python inputs (#330)

These are **inputs, not expected answers**. The six consumers only read them;
normal test runs never regenerate or repair this directory. Arithmetic still uses
Python's exact independent oracle, guard overlap uses `reference.py`, and all
existing ZIP/writer/canonical checks remain independent of production results.
The boundary cases in those tests are unchanged.

`manifest.json` records original source paths and SHA-256, the pre-freeze Git
commit, scenario, seed, element count, byte count and SHA-256 for every fixture.
It also hashes every fully materialized damaged archive. JSON is UTF-8, two-space
indentation, LF and a final newline; integers are exact signed int64 values, not
floating-point numbers. Binary files are raw bytes. No expectation is frozen.

| Consumer | Input | Count / seed | Existing CTest consumer |
|---|---|---|---|
| `scripts/check_module_abi.py` | `abi-guard-pairs.json` | 600 pairs / 209 | `integration.module_abi` |
| `scripts/check_module_canonical.py` | `canonical-key-orders.json` | 20 orders of 13 keys / 180 | `integration.module_canonical` |
| `scripts/check_module_container.py` | `container-damage.json`, `container-damage-base.bin` | 100 cases, 3 ordered XOR mutations each / 182 | `integration.module_container` |
| `scripts/check_module_writer.py` | `writer-payload.bin`, `writer-native-order.json` | 65,537 bytes then 12-index shuffle / 183 | `integration.module_writer` |
| `scripts/check_zip.py` | `zip-payload.bin`, `zip-damage.json`, `zip-damage-base*.bin` | 65,537 bytes then 60 cases of 3 XOR mutations / 247 | `integration.zip` |
| `test/src/native/checks/saturation.py` | `saturation-pairs.json` | 1,000 operand pairs / 20261006 | `native.native` (manifest check `saturation`) |

The original saturation path was `test/native/checks/saturation.py` before #320.
The other five source paths are unchanged. The shared RNG stream ordering is
preserved: the writer shuffle and ZIP mutations follow all 65,537 payload draws.
The mutation recipe is an ordered array of cases; each case has three
`[byte_offset, xor_mask]` pairs, applied in order (repeated offsets are permitted).
The frozen base plus mutations defines each complete input byte-for-byte.

Python's original ZIP fixture used the platform default `ZipInfo.create_system`:
3 on Linux and 0 on Windows. Both base archives are preserved; Windows selects
`zip-damage-base-windows.bin`, Linux the other base. The exporter explicitly sets
both defaults, so its results do not depend on the exporting OS. The container
builder already used 3 on every platform. These base archives were reproduced
with CPython 3.12.14 / zlib 1.3.2; cross-platform CI also checks reproduction with
Python 3.13. Any different compression output is a failing comparison, never an
automatic fixture refresh.

## Reproduction and verification

Run from the repository root:

```sh
python test/support/freeze_python_samples.py --check
python test/support/freeze_python_samples.py --self-test
python test/support/verify_original_python_samples.py --baseline 89b8d80ba13dfa3cb5ca9d95b1448d45fec438f2
# Explicit export into build, not the committed corpus:
python test/support/freeze_python_samples.py --write --output build/reproduced-python-samples
```

`--check` compares every byte with the preserved original recipes and checks the
manifest, without writes. `--self-test` verifies corruption and missing-file
rejection without repairs, in a unique temporary build directory. The optional
Git-history audit executes selected original AST nodes (never production hosts
or expected-result code), comparing the old streams element-for-element and
byte-for-byte. It requires the baseline commit locally; the ordinary check does
not require Git or network access. To intentionally replace the corpus, use
`--write` without `--output`, review the diff, and re-run the original audit.

CTest IDs `tool.python-frozen-samples` and
`tool.python-frozen-samples-self-test` are added without changing the 59 existing
integration registrations. The module-format CI also runs both checks on Linux
x86-64/AArch64 and Windows. No C build target or explicit shell/cmd target list
changes are needed: no executable was added.

The four `test/support/*python_samples*.py` helpers are temporary migration
export/audit tools, not production dependencies. Their removal is tracked under
#395; subsequent C consumers read these same inputs and use independent oracles.
