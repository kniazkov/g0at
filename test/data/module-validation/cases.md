# Original → CTest mapping (#366)

Original line numbers refer to baseline `e95f826f8aaa0e6f557dbdd6618e06e2efe4e4e2`.
Every row keeps the original status and all recorded diagnostic fields/order.

| Original case / line | Stable CTest ID | Expected status |
|---|---|---|
| compiler.valid / 127 | `integration.module_validation.compiler.valid` | 0 |
| compiler.repeat / 128 | `integration.module_validation.compiler.repeat` | 0 |
| compiler.without-native / 129 | `integration.module_validation.compiler.without-native` | 0 |
| compiler.bad-opcode / 131 | `integration.module_validation.compiler.bad-opcode` | 5 |
| compiler.wrong-instruction / 133 | `integration.module_validation.compiler.wrong-instruction` | 5 |
| compiler.wrong-symbol / 135 | `integration.module_validation.compiler.wrong-symbol` | 5 |
| loader.valid / 127 | `integration.module_validation.loader.valid` | 0 |
| loader.repeat / 128 | `integration.module_validation.loader.repeat` | 0 |
| loader.without-native / 129 | `integration.module_validation.loader.without-native` | 0 |
| loader.bad-opcode / 131 | `integration.module_validation.loader.bad-opcode` | 5 |
| loader.wrong-instruction / 133 | `integration.module_validation.loader.wrong-instruction` | 5 |
| loader.wrong-symbol / 135 | `integration.module_validation.loader.wrong-symbol` | 5 |
| inspect.valid / 127 | `integration.module_validation.inspect.valid` | 0 |
| inspect.repeat / 128 | `integration.module_validation.inspect.repeat` | 0 |
| inspect.without-native / 129 | `integration.module_validation.inspect.without-native` | 0 |
| inspect.bad-opcode / 131 | `integration.module_validation.inspect.bad-opcode` | 5 |
| inspect.wrong-instruction / 133 | `integration.module_validation.inspect.wrong-instruction` | 5 |
| inspect.wrong-symbol / 135 | `integration.module_validation.inspect.wrong-symbol` | 5 |
| merge.valid / 127 | `integration.module_validation.merge.valid` | 0 |
| merge.repeat / 128 | `integration.module_validation.merge.repeat` | 0 |
| merge.without-native / 129 | `integration.module_validation.merge.without-native` | 0 |
| merge.bad-opcode / 131 | `integration.module_validation.merge.bad-opcode` | 5 |
| merge.wrong-instruction / 133 | `integration.module_validation.merge.wrong-instruction` | 5 |
| merge.wrong-symbol / 135 | `integration.module_validation.merge.wrong-symbol` | 5 |
| external-uninstalled / 142 | `integration.module_validation.external-uninstalled` | 0 |
| dependencies-absent / 147 | `integration.module_validation.dependencies-absent` | 0 |
| dependencies-empty / 147 | `integration.module_validation.dependencies-empty` | 0 |
| schema-two-errors / 151 | `integration.module_validation.schema-two-errors` | 5 |
| schema-repeat / 155 | `integration.module_validation.schema-repeat` | 5 |
| json-open-object / 157 | `integration.module_validation.json-open-object` | 5 |
| json-truncated / 157 | `integration.module_validation.json-truncated` | 5 |
| json-null / 157 | `integration.module_validation.json-null` | 5 |
| archive-empty / 163 | `integration.module_validation.archive-empty` | 5 |
| archive-not-zip / 163 | `integration.module_validation.archive-not-zip` | 5 |
| archive-short-header / 163 | `integration.module_validation.archive-short-header` | 5 |
| archive-truncated-tail / 163 | `integration.module_validation.archive-truncated-tail` | 5 |
| crc-native / 176 | `integration.module_validation.crc-native` | 5 |
| crc-native-and-bytecode-semantic / 178 | `integration.module_validation.crc-native-and-bytecode-semantic` | 5 |
| crc-bytecode-and-native-semantic / 182 | `integration.module_validation.crc-bytecode-and-native-semantic` | 5 |
| crc-metadata-and-bytecode / 190 | `integration.module_validation.crc-metadata-and-bytecode` | 5 |
| undeclared-native / 196 | `integration.module_validation.undeclared-native` | 5 |
| hash-main-and-native / 204 | `integration.module_validation.hash-main-and-native` | 5 |
| hash-main / 208 | `integration.module_validation.hash-main` | 5 |
| hash-native / 209 | `integration.module_validation.hash-native` | 5 |
| foreign-abi / 217 | `integration.module_validation.foreign-abi` | 0 |
| foreign-wrong-symbol / 219 | `integration.module_validation.foreign-wrong-symbol` | 5 |
| two-wrong-symbols / 221 | `integration.module_validation.two-wrong-symbols` | 5 |
| future-cpu-feature / 223 | `integration.module_validation.future-cpu-feature` | 0 |
| invalid-cpu-feature / 225 | `integration.module_validation.invalid-cpu-feature` | 5 |
| unknown-function / 227 | `integration.module_validation.unknown-function` | 5 |
| limit-archive / 230 | `integration.module_validation.limit-archive` | 3 |
| limit-expanded / 231 | `integration.module_validation.limit-expanded` | 3 |
| limit-exact / 232 | `integration.module_validation.limit-exact` | 0 |
| argument/null/snapshot API smoke / 123 | `integration.module_validation.api` | OK (0) |
| UTF-8 file snapshot/replacement / 238–239 | `integration.module_validation.file-default` | OK (0) |
| exact archive limit and replacement / 238–239 | `integration.module_validation.file-exact-limit` | OK (0) |
| archive too large; input unchanged / 241 | `integration.module_validation.file-archive-limit` | LIMIT (3) |
| missing path / 242 | `integration.module_validation.file-missing` | IO (2) |
| real constructor proof + good/bad validation / 245–279 (compiler) | `integration.module_validation.constructor.compiler` | OK (0), CORRUPT (5) |
| real constructor proof + good/bad validation / 245–279 (loader) | `integration.module_validation.constructor.loader` | OK (0), CORRUPT (5) |
| real constructor proof + good/bad validation / 245–279 (inspect) | `integration.module_validation.constructor.inspect` | OK (0), CORRUPT (5) |
| real constructor proof + good/bad validation / 245–279 (merge) | `integration.module_validation.constructor.merge` | OK (0), CORRUPT (5) |

## Predicate groups

- Role matrix: valid/repeated diagnostics, native absent, invalid instruction opcode, metadata instruction mismatch, native symbol mismatch.
- External declaration bytes: uninstalled dependency is accepted by archive validation; missing and empty dependencies both remain accepted.
- Schema/JSON: both `/name` and `/version` errors survive; malformed JSON produces exactly one failure; no invented hash failure; bytecode/native skipped.
- Container: empty/non-ZIP/truncated archive; native CRC; CRC plus independent bytecode/native semantic failure; bytecode prerequisite references container entry; two independent CRC failures without derived schema/hash errors.
- Undeclared native payload: hash stage is skipped with prerequisite3 and never falsely passes.
- Hashes: independent main/library failures survive together, safe bytecode semantics still pass.
- Native metadata: foreign/future ABI and capabilities do not suppress validation, both bad records reported; invalid CPU feature and unknown function rejected.
- Limits: archive length−1, expanded1, exact archive boundary; file replacement/retained snapshots and missing path.
- Real nonexecution: constructor proven executable first, then marker absent after both valid and damaged archive validation for every role.

The source `run()` universal predicates are checked separately from the captured complete event oracle. Repeated cases are not elided.
