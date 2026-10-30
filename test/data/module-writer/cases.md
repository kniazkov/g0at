# Original → CTest mapping (#367)

Baseline: `0c4de46b:scripts/check_module_writer.py`. Status sets and limits are unchanged.

| Original case / line | Stable CTest ID | Allowed status |
|---|---|---|
| golden / 98 | `integration.module_writer.golden` | [0] |
| golden-forward / 102 | `integration.module_writer.golden-forward` | [0] |
| golden-reverse / 102 | `integration.module_writer.golden-reverse` | [0] |
| normalize-dependencies / 108 | `integration.module_writer.normalize-dependencies` | [0] |
| normalize-native / 108 | `integration.module_writer.normalize-native` | [0] |
| normalize-authors / 108 | `integration.module_writer.normalize-authors` | [0] |
| normalize-functions / 108 | `integration.module_writer.normalize-functions` | [0] |
| normalize-externals / 108 | `integration.module_writer.normalize-externals` | [0] |
| normalize-description / 108 | `integration.module_writer.normalize-description` | [0] |
| normalize-license / 108 | `integration.module_writer.normalize-license` | [0] |
| normalize-homepage / 108 | `integration.module_writer.normalize-homepage` | [0] |
| information / 113 | `integration.module_writer.information` | [0] |
| information-reverse / 114 | `integration.module_writer.information-reverse` | [0] |
| native-profile / 157 | `integration.module_writer.native-profile` | [0] |
| native-reverse / 162 | `integration.module_writer.native-reverse` | [0] |
| native-shuffled / 167 | `integration.module_writer.native-shuffled` | [0] |
| native-renamed / 171 | `integration.module_writer.native-renamed` | [0] |
| native-tied / 183 | `integration.module_writer.native-tied` | [0] |
| native-tied-reverse / 185 | `integration.module_writer.native-tied-reverse` | [0] |
| invalid-main-hash / 211 | `integration.module_writer.invalid-main-hash` | [1, 2, 3, 6] |
| invalid-unknown-field / 211 | `integration.module_writer.invalid-unknown-field` | [1, 2, 3, 6] |
| invalid-null-dependencies / 211 | `integration.module_writer.invalid-null-dependencies` | [1, 2, 3, 6] |
| invalid-opcode / 211 | `integration.module_writer.invalid-opcode` | [1, 2, 3, 6] |
| invalid-native-hash / 211 | `integration.module_writer.invalid-native-hash` | [1, 2, 3, 6] |
| invalid-missing-payload / 211 | `integration.module_writer.invalid-missing-payload` | [1, 2, 3, 6] |
| invalid-wrong-extension / 211 | `integration.module_writer.invalid-wrong-extension` | [1, 2, 3, 6] |
| invalid-reserved-name / 211 | `integration.module_writer.invalid-reserved-name` | [1, 2, 3, 6] |
| invalid-extra-payload / 211 | `integration.module_writer.invalid-extra-payload` | [1, 2, 3, 6] |
| invalid-unknown-function / 211 | `integration.module_writer.invalid-unknown-function` | [1, 2, 3, 6] |
| limit-zero / 213 | `integration.module_writer.limit-zero` | [4, 5, 6] |
| limit-one / 213 | `integration.module_writer.limit-one` | [4, 5, 6] |
| limit-before-golden / 213 | `integration.module_writer.limit-before-golden` | [4, 5, 6] |
| limit-exact-golden / 214 | `integration.module_writer.limit-exact-golden` | [0] |
| limit-expanded / 215 | `integration.module_writer.limit-expanded` | [4, 6] |
| null/argument/limit API checks / 96 | `integration.module_writer.api` | 0 |
| new UTF-8 destination bytes / 220–221 | `integration.module_writer.publication.new` | 0 |
| atomic replacement and mode0640 / 223–234 | `integration.module_writer.publication.replace` | 0 |
| old hard link retains full bytes; restore / 235–241 | `integration.module_writer.publication.hardlink` | 0 |
| retained input equals output destination / 242–246 | `integration.module_writer.publication.same-output` | 0 |
| missing destination parent / 253 | `integration.module_writer.publication.missing-parent` | 7,8 |
| directory destination and no leaked entry / 254–255 | `integration.module_writer.publication.directory` | 7,8 |
| Windows1314 only skip; target/link intact / 259–271 | `integration.module_writer.publication.symlink` | 7 |
| POSIX FIFO rejection and identity / 271–275 | `integration.module_writer.publication.fifo` | 7 |
| write injection, old bytes and tempfiles / 256–258 | `integration.module_writer.publication.fault-write` | 8 |
| close injection, old bytes and tempfiles / 256–258 | `integration.module_writer.publication.fault-close` | 8 |
| validation injection, old bytes and tempfiles / 256–258 | `integration.module_writer.publication.fault-validation` | 6 |
| prepare injection, old bytes and tempfiles / 256–258 | `integration.module_writer.publication.fault-prepare` | 8 |
| rename injection, old bytes and tempfiles / 256–258 | `integration.module_writer.publication.fault-rename` | 8 |
| invalid-main-hash publication: old bytes, mode, no tempfiles / 248–250 | `integration.module_writer.publication.invalid-main-hash` | [1, 2, 3, 6] |
| invalid-unknown-field publication: old bytes, mode, no tempfiles / 248–250 | `integration.module_writer.publication.invalid-unknown-field` | [1, 2, 3, 6] |
| invalid-null-dependencies publication: old bytes, mode, no tempfiles / 248–250 | `integration.module_writer.publication.invalid-null-dependencies` | [1, 2, 3, 6] |
| invalid-opcode publication: old bytes, mode, no tempfiles / 248–250 | `integration.module_writer.publication.invalid-opcode` | [1, 2, 3, 6] |
| invalid-native-hash publication: old bytes, mode, no tempfiles / 248–250 | `integration.module_writer.publication.invalid-native-hash` | [1, 2, 3, 6] |
| invalid-missing-payload publication: old bytes, mode, no tempfiles / 248–250 | `integration.module_writer.publication.invalid-missing-payload` | [1, 2, 3, 6] |
| invalid-wrong-extension publication: old bytes, mode, no tempfiles / 248–250 | `integration.module_writer.publication.invalid-wrong-extension` | [1, 2, 3, 6] |
| invalid-reserved-name publication: old bytes, mode, no tempfiles / 248–250 | `integration.module_writer.publication.invalid-reserved-name` | [1, 2, 3, 6] |
| invalid-extra-payload publication: old bytes, mode, no tempfiles / 248–250 | `integration.module_writer.publication.invalid-extra-payload` | [1, 2, 3, 6] |
| invalid-unknown-function publication: old bytes, mode, no tempfiles / 248–250 | `integration.module_writer.publication.invalid-unknown-function` | [1, 2, 3, 6] |
| limit-one publication: old bytes, mode, no tempfiles / 251–252 | `integration.module_writer.publication.limit-one` | [4, 5, 6] |

34 memory + 24 publication + 1 API = **59 independent IDs**.

The native profile includes empty, one/two-byte, repeated, byte-range and seeded
65,537-byte payloads;12 mixed Windows/Unix variants; reversed input entry order;
the original continued-RNG shuffle; normalized/renamed idempotence; and tied
target/hash keys with per-record compiler descriptions. All JSON and hash negatives
are frozen from the original without repair. See [contracts and evidence](README.md).
