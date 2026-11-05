# Frozen pre-migration Python inputs (#330)

These are **inputs, not expected answers**. The six consumers only read them;
normal test runs never regenerate or repair this directory. The migrated C
consumers use independent integer/format oracles and preserve the original
ZIP/writer/canonical checks and boundary cases. Historical Python source names
below identify the provenance of the frozen inputs, not current dependencies.

`manifest.json` records original source paths and SHA-256, the pre-freeze Git
commit, scenario, seed, element count, byte count and SHA-256 for every fixture.
It also hashes every fully materialized damaged archive. JSON is UTF-8, two-space
indentation, LF and a final newline; integers are exact signed int64 values, not
floating-point numbers. Binary files are raw bytes. No expectation is frozen.

| Original source / migrated consumer | Input | Count / seed | Current CTest consumer |
|---|---|---|---|
| `scripts/check_module_abi.py` | `abi-guard-pairs.json` | 600 pairs / 209 | `integration.module_abi` |
| `scripts/check_module_canonical.py` | `canonical-key-orders.json` | 20 orders of 13 keys / 180 | `native.module_canonical` |
| `scripts/check_module_container.py` | `container-damage.json`, `container-damage-base.bin` | 100 cases, 3 ordered XOR mutations each / 182 | `integration.module_container` |
| `module_writer_scenarios` (frozen original writer inputs, #367) | `writer-payload.bin`, `writer-native-order.json` | 65,537 bytes then 12-index shuffle / 183 | `integration.module_writer.native-*` |
| `scripts/check_zip.py` | `zip-payload.bin`, `zip-damage.json`, `zip-damage-base*.bin` | 65,537 bytes then 60 cases of 3 XOR mutations / 247 | `integration.zip` |
| `test/src/native/checks/saturation.py` | `saturation-pairs.json` | 1,000 operand pairs / 20261006 | `integration.native_saturation.*` (also native manifest check `saturation`) |

The original saturation path was `test/native/checks/saturation.py` before #320.
Historical source paths in the manifest identify the pre-freeze baseline;
all six consumers have since migrated to C. The shared RNG stream ordering is
preserved: the writer shuffle and ZIP mutations follow all 65,537 payload draws.
The mutation recipe is an ordered array of cases; each case has three
`[byte_offset, xor_mask]` pairs, applied in order (repeated offsets are permitted).
The frozen base plus mutations defines each complete input byte-for-byte.

Python's original ZIP fixture used the platform default `ZipInfo.create_system`:
3 on Linux and 0 on Windows. Both base archives are preserved; Windows selects
`zip-damage-base-windows.bin`, Linux the other base. The exporter explicitly sets
both defaults, so its results do not depend on the exporting OS. The container
builder already used 3 on every platform. These base archives were reproduced
with CPython 3.12.14 / zlib 1.3.2; the historical cross-platform parity gate also
checked reproduction with Python 3.13. Current CI validates pinned bytes and all
materialized mutation hashes with C, without executing Python recipes. Optional
recipe reproduction still rejects different compression output rather than
automatically refreshing fixtures.

## Current integrity verification

Run from the repository root after configuring a build:

```sh
cmake --build build --target frozen_samples
build/frozen_samples --check --data test/data/python-samples
build/frozen_samples --self-test --data test/data/python-samples --build build
ctest --test-dir build -L frozen-samples-c --output-on-failure
```

The C gate verifies the pinned manifest, all eleven fixture lengths/digests and
all 220 fully materialized damaged-input digests. It does not reproduce CPython
RNG or zlib recipes. Its self-test rejects missing/corrupt files and damaged-input
digest mismatches without repairing source inputs. See the
[complete integrity and control contract](../../support/frozen_samples.md).
`tool.frozen-samples` and `tool.frozen-samples-self-test` select these C checks;
the historical `tool.python-frozen-samples` and
`tool.python-frozen-samples-self-test` IDs remain C aliases, not Python commands.
Module-format CI runs the C checks on Linux x86-64/AArch64 and Windows.

## Optional historical recipe reproduction

The following four Python generation/audit helpers remain available but inactive
in CMake, CTest and CI until #395. These explicit commands require a compatible
Python/zlib installation; they are separate from ordinary integrity checks:

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
not require Git or network access. Use the separate `--output` directory for
reproduction review; normal checks never replace the committed corpus.

The four retained files are `test/support/freeze_python_samples.py`,
`test/support/python_samples_binary.py`, `test/support/python_samples_structured.py`
and `test/support/verify_original_python_samples.py`. They are inactive migration
export/audit tools, not production or test dependencies; their removal remains
tracked under #395. The current C consumers read the same unchanged inputs.

The writer migration keeps these sample files and provenance manifests unchanged.
Its exact input archives embed the original payload/permutation; see
[writer scenario map](../module-writer/README.md). The optional original-recipe
audit uses `git show --baseline` history, so retirement of the live Python runner
does not require keeping an import shim.
