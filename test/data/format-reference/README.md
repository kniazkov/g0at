# Reference unittest retirement map (#394)

The 21 methods of `docs/module-format-v1/test_reference.py` at parent
`4a90f8c0ca9ab96d9ee1a9a7887dc17ebda052ee` retain the following independent C
coverage. This directory documents the mapping only; no vectors, schemas or
frozen corpus bytes were changed. The Python methods remain enabled during the
initial parity phase and are retired only after its complete CI matrix passes.

| Original method | CTest coverage |
| --- | --- |
| `test_schema_snapshots` | `format.schema`, `format.schema.roundtrip`; opcode names and exact indices in `format.oracle.reference-artifacts` |
| `test_declarative_schema_source` | `format.schema` and `format.schema.roundtrip` compare all schema output bytes; `format.oracle.reference-artifacts` asserts source/root order and exact signed 64-bit range bounds |
| `test_schema_source_rejects_lossy_input` | `format.schema.reject-lossy-source` replays all 11 original raw JSON inputs through the independent C generator; existing `reject-float`, `reject-duplicate` and other generator rejection tests remain |
| `test_summary_consistency` | `format.oracle.summary_consistency` (76 frozen calls) |
| `test_strict_json` | `format.oracle.strict_json` (9 calls) |
| `test_canonical_bytes` | `format.oracle.canonical_bytes` (6 calls) |
| `test_shapes` | `format.oracle.shapes` (18 calls) |
| `test_gmod` | `format.oracle.gmod` (7 calls) |
| `test_constants` | `format.oracle.constants` (20 calls) |
| `test_facts_normalization` | `format.oracle.facts_normalization` (11 calls) |
| `test_summaries` | `format.oracle.summaries` (5 calls) |
| `test_dependency_versions` | `format.oracle.dependency_versions` (4 calls) |
| `test_cross_part_validation` | `format.binary.modules` (25 frozen cases, including all original cross-part inputs) |
| `test_hash_projection` | `format.oracle.hash_projection` (6 calls); full preimage/digest assertions in `format.binary.vectors` |
| `test_bytecode_layout` | `format.binary.bytecode` (1,195 cases); literal 112-byte/header/section/END layout checks and binary/hex equality in `format.binary.vectors` |
| `test_archive_with_independent_reader` | `format.binary.zip` checks names/order/count, compression, DOS timestamp, Unix permissions, contents and CRC; `format.binary.vectors` checks archive binary/hex equality and SHA-256 |
| `test_native_envelope` | `format.oracle.native_envelope` (3 JSON calls); `format.binary.vectors` checks magic, LE64 length, terminator, payload bytes and normalization |
| `test_target` | `format.oracle.target` (3 calls) |
| `test_guard_intersections` | `format.oracle.guard_intersections` (4 calls) |
| `test_normalization_preserves_distinctions` | `format.oracle.normalization_preserves_distinctions` (7 calls) |
| `test_hash_excludes_native_but_validates_it` | `format.oracle.hash_excludes_native_but_validates_it` (6 calls) |

Python alias-object identity assertions (`NAME`, `HASH`, `DEFS`, `ROOTS`) applied
to the removed Python compatibility API, not to serialized format behavior.
Their continuing contract is a single declarative source, exact serialized
schema snapshots and exact integer values, all checked above. C does not retain
unused Python aliases merely to test their object identity.

The new source rejection scenario also proves that both generator modes leave
all five existing outputs and the invalid source unchanged. Other schema tests
cover stale/missing outputs. Existing oracle corrupt-output and accept-invalid
negative controls remain, with independent support code linked instead of the
production validator. Both `all` oracle modes (including UBSan) exercise the
strengthened assertions. See the [JSON corpus](../format-oracle/README.md) and
[binary corpus](../binary-oracle/README.md) for frozen same-input provenance,
counts, digest checks and fault isolation.

## Reproduce the assertion checks

```sh
cmake -S . -B build/format -DCMAKE_BUILD_TYPE=Debug
cmake --build build/format --target format_oracle_testing format_binary_testing module_schema_generator_testing
ctest --test-dir build/format -R '^format\.(oracle|binary|schema)(\.|$)' -E '\.ubsan$' --output-on-failure
```

During initial parity, also run the original 21 methods before retirement:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 docs/module-format-v1/test_reference.py
```

This is the unittest mapping, not a replacement for the separate vector
regeneration CLI or frozen-sample integrity checks. Those contracts must remain
available through C tooling before the active Python entry points are removed.

## Complete vector generator

`format_vectors` replaces the complete `reference.py` CLI, not only schema
serialization. It independently generates 21 vectors, the opcode map and four
schemas using test-layer binary/ZIP/JSON helpers. The default and `--check` are
read-only. `--write` requires an explicit `--root`; `--source` allows a separate
schema source when writing a fresh destination. All 26 results were compared
byte-for-byte with separately executed historical Python generation.

```sh
cmake --build build/format --target format_vectors frozen_samples
build/format/format_vectors --check --root .
ctest --test-dir build/format -R '^format[.]vectors' --output-on-failure
build/format/format_vectors --write --root /tmp/reviewed-vector-output --source docs/module-format-v1/schema-source.json
```

`format.vectors.controls` owns a unique build child and retains it on failure.
It checks all 26 files for roundtrip, missing and stale detection without repair,
and all eleven original invalid schema sources before any output creation or
change. Windows paths use the shared UTF-8 filesystem interface. No production
format validator is used by the oracle/generator; filesystem services are shared.

The separate [frozen input gate](../../support/frozen_samples.md) preserves exact
fixture/provenance bytes and damaged-input digests without invoking Python RNG
or compressor reproduction during normal CI. Original Python checks remain
active alongside both new gates until initial full CI parity is complete.
