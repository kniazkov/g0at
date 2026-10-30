# Complete original-to-C scenario map (#364)

All line references refer to baseline `305ae938fe52c87c8afdfc443d15b3d7cd989868`.

| Original script | Lines | Stable CTest ID | Host calls |
| --- | --- | --- | ---: |
| `check_module_information.py` | 60–67 | `integration.module_information.no-network-symbols` | 1 (`nm`) |
| `check_module_information.py` | 69 | `integration.module_information.base-hash` | 1 |
| `check_module_information.py` | 73 | `integration.module_information.empty-writer.description` | 1 |
| `check_module_information.py` | 73 | `integration.module_information.empty-writer.license` | 1 |
| `check_module_information.py` | 73 | `integration.module_information.empty-writer.homepage` | 1 |
| `check_module_information.py` | 73 | `integration.module_information.empty-writer.authors` | 1 |
| `check_module_information.py` | 73 | `integration.module_information.empty-writer.producers` | 1 |
| `check_module_information.py` | 77 | `integration.module_information.gmod.empty` | 1 |
| `check_module_information.py` | 81, 83 | `integration.module_information.gmod.field.description` | 6 |
| `check_module_information.py` | 81, 83 | `integration.module_information.gmod.field.license` | 6 |
| `check_module_information.py` | 81, 83 | `integration.module_information.gmod.field.homepage` | 6 |
| `check_module_information.py` | 81, 83 | `integration.module_information.gmod.field.authors` | 6 |
| `check_module_information.py` | 90, 93, 94, 95 | `integration.module_information.gmod.text.00` | 4 |
| `check_module_information.py` | 90, 93, 94, 95 | `integration.module_information.gmod.text.01` | 4 |
| `check_module_information.py` | 90, 93, 94, 95 | `integration.module_information.gmod.text.02` | 4 |
| `check_module_information.py` | 90, 93, 94, 95 | `integration.module_information.gmod.text.03` | 4 |
| `check_module_information.py` | 90, 93, 94, 95 | `integration.module_information.gmod.text.04` | 4 |
| `check_module_information.py` | 90, 93, 94, 95 | `integration.module_information.gmod.text.05` | 4 |
| `check_module_information.py` | 90, 93, 94, 95 | `integration.module_information.gmod.text.06` | 4 |
| `check_module_information.py` | 90, 93, 94, 95 | `integration.module_information.gmod.text.07` | 4 |
| `check_module_information.py` | 90, 93, 94, 95 | `integration.module_information.gmod.text.08` | 4 |
| `check_module_information.py` | 90, 93, 94, 95 | `integration.module_information.gmod.text.09` | 4 |
| `check_module_information.py` | 90, 93, 94, 95 | `integration.module_information.gmod.text.10` | 4 |
| `check_module_information.py` | 104 | `integration.module_information.gmod.author-type.name` | 5 |
| `check_module_information.py` | 104 | `integration.module_information.gmod.author-type.email` | 5 |
| `check_module_information.py` | 106 | `integration.module_information.gmod.author-shape.00` | 1 |
| `check_module_information.py` | 106 | `integration.module_information.gmod.author-shape.01` | 1 |
| `check_module_information.py` | 106 | `integration.module_information.gmod.author-shape.02` | 1 |
| `check_module_information.py` | 109, 110 | `integration.module_information.gmod.author-order` | 2 |
| `check_module_information.py` | 111 | `integration.module_information.gmod.empty-email` | 2 |
| `check_module_information.py` | 116 | `integration.module_information.gmod.author-duplicate.00` | 1 |
| `check_module_information.py` | 116 | `integration.module_information.gmod.author-duplicate.01` | 1 |
| `check_module_information.py` | 116 | `integration.module_information.gmod.author-duplicate.02` | 1 |
| `check_module_information.py` | 118 | `integration.module_information.gmod.escaped-author-duplicate` | 1 |
| `check_module_information.py` | 120 | `integration.module_information.gmod.malformed-utf8.00` | 1 |
| `check_module_information.py` | 120 | `integration.module_information.gmod.malformed-utf8.01` | 1 |
| `check_module_information.py` | 120 | `integration.module_information.gmod.malformed-utf8.02` | 1 |
| `check_module_information.py` | 122 | `integration.module_information.gmod.invalid-version` | 1 |
| `check_module_information.py` | 123 | `integration.module_information.gmod.unknown-property` | 1 |
| `check_module_information.py` | 77 | `integration.module_information.module.empty` | 1 |
| `check_module_information.py` | 81, 83 | `integration.module_information.module.field.description` | 6 |
| `check_module_information.py` | 81, 83 | `integration.module_information.module.field.license` | 6 |
| `check_module_information.py` | 81, 83 | `integration.module_information.module.field.homepage` | 6 |
| `check_module_information.py` | 81, 83 | `integration.module_information.module.field.authors` | 6 |
| `check_module_information.py` | 81, 83 | `integration.module_information.module.field.producers` | 6 |
| `check_module_information.py` | 90, 93, 94, 95, 97, 98, 99, 100 | `integration.module_information.module.text.00` | 8 |
| `check_module_information.py` | 90, 93, 94, 95, 97, 98, 99, 100 | `integration.module_information.module.text.01` | 8 |
| `check_module_information.py` | 90, 93, 94, 95, 97, 98, 99, 100 | `integration.module_information.module.text.02` | 8 |
| `check_module_information.py` | 90, 93, 94, 95, 97, 98, 99, 100 | `integration.module_information.module.text.03` | 8 |
| `check_module_information.py` | 90, 93, 94, 95, 97, 98, 99, 100 | `integration.module_information.module.text.04` | 8 |
| `check_module_information.py` | 90, 93, 94, 95, 97, 98, 99, 100 | `integration.module_information.module.text.05` | 8 |
| `check_module_information.py` | 90, 93, 94, 95, 97, 98, 99, 100 | `integration.module_information.module.text.06` | 8 |
| `check_module_information.py` | 90, 93, 94, 95, 97, 98, 99, 100 | `integration.module_information.module.text.07` | 8 |
| `check_module_information.py` | 90, 93, 94, 95, 97, 98, 99, 100 | `integration.module_information.module.text.08` | 8 |
| `check_module_information.py` | 90, 93, 94, 95, 97, 98, 99, 100 | `integration.module_information.module.text.09` | 8 |
| `check_module_information.py` | 90, 93, 94, 95, 97, 98, 99, 100 | `integration.module_information.module.text.10` | 8 |
| `check_module_information.py` | 104 | `integration.module_information.module.author-type.name` | 5 |
| `check_module_information.py` | 104 | `integration.module_information.module.author-type.email` | 5 |
| `check_module_information.py` | 106 | `integration.module_information.module.author-shape.00` | 1 |
| `check_module_information.py` | 106 | `integration.module_information.module.author-shape.01` | 1 |
| `check_module_information.py` | 106 | `integration.module_information.module.author-shape.02` | 1 |
| `check_module_information.py` | 109, 110 | `integration.module_information.module.author-order` | 2 |
| `check_module_information.py` | 111 | `integration.module_information.module.empty-email` | 2 |
| `check_module_information.py` | 116 | `integration.module_information.module.author-duplicate.00` | 1 |
| `check_module_information.py` | 116 | `integration.module_information.module.author-duplicate.01` | 1 |
| `check_module_information.py` | 116 | `integration.module_information.module.author-duplicate.02` | 1 |
| `check_module_information.py` | 118 | `integration.module_information.module.escaped-author-duplicate` | 1 |
| `check_module_information.py` | 120 | `integration.module_information.module.malformed-utf8.00` | 1 |
| `check_module_information.py` | 120 | `integration.module_information.module.malformed-utf8.01` | 1 |
| `check_module_information.py` | 120 | `integration.module_information.module.malformed-utf8.02` | 1 |
| `check_module_information.py` | 122 | `integration.module_information.module.invalid-version` | 1 |
| `check_module_information.py` | 123 | `integration.module_information.module.unknown-property` | 1 |
| `check_module_information.py` | 126, 127 | `integration.module_information.scalar-record.00` | 2 |
| `check_module_information.py` | 126, 127 | `integration.module_information.scalar-record.01` | 2 |
| `check_module_information.py` | 126, 127 | `integration.module_information.scalar-record.02` | 2 |
| `check_module_information.py` | 126, 127 | `integration.module_information.scalar-record.03` | 2 |
| `check_module_information.py` | 126, 127 | `integration.module_information.scalar-record.04` | 2 |
| `check_module_information.py` | 131 | `integration.module_information.producer-type.name` | 6 |
| `check_module_information.py` | 133 | `integration.module_information.producer-missing.name` | 1 |
| `check_module_information.py` | 131 | `integration.module_information.producer-type.version` | 6 |
| `check_module_information.py` | 133 | `integration.module_information.producer-missing.version` | 1 |
| `check_module_information.py` | 131 | `integration.module_information.producer-type.buildId` | 6 |
| `check_module_information.py` | 133 | `integration.module_information.producer-missing.buildId` | 1 |
| `check_module_information.py` | 134 | `integration.module_information.producer-timestamp` | 1 |
| `check_module_information.py` | 135 | `integration.module_information.producer-machine` | 1 |
| `check_module_information.py` | 136 | `integration.module_information.producer-duplicate` | 1 |
| `check_module_information.py` | 137 | `integration.module_information.gmod-producers` | 1 |
| `check_module_information.py` | 139 | `integration.module_information.escaped-producer-duplicate` | 1 |
| `check_module_information.py` | 147, 148, 149, 150, 151 | `integration.module_information.producer-order-idempotence` | 10 |
| `check_module_information.py` | 156, 157 | `integration.module_information.field-hash.description` | 2 |
| `check_module_information.py` | 156, 157 | `integration.module_information.field-hash.license` | 2 |
| `check_module_information.py` | 156, 157 | `integration.module_information.field-hash.homepage` | 2 |
| `check_module_information.py` | 156, 157 | `integration.module_information.field-hash.authors` | 2 |
| `check_module_information.py` | 156, 157 | `integration.module_information.field-hash.producers` | 2 |
| `check_module_information.py` | 160, 161, 164 | `integration.module_information.text-distinction.00` | 6 |
| `check_module_information.py` | 160, 161, 164 | `integration.module_information.text-distinction.01` | 6 |
| `check_module_information.py` | 160, 161, 164 | `integration.module_information.text-distinction.02` | 6 |
| `check_module_information.py` | 160, 161, 164 | `integration.module_information.text-distinction.03` | 6 |
| `check_module_information.py` | 160, 161 | `integration.module_information.text-distinction.04` | 4 |
| `check_module_information_merge.py` | 41 | `integration.module_information_merge.zero-inputs` | 1 |
| `check_module_information_merge.py` | 42 | `integration.module_information_merge.one-empty` | 1 |
| `check_module_information_merge.py` | 43 | `integration.module_information_merge.three-empty` | 1 |
| `check_module_information_merge.py` | 47, 49 | `integration.module_information_merge.empty-field.description` | 7 |
| `check_module_information_merge.py` | 47, 49 | `integration.module_information_merge.empty-field.license` | 7 |
| `check_module_information_merge.py` | 47, 49 | `integration.module_information_merge.empty-field.homepage` | 7 |
| `check_module_information_merge.py` | 47, 49 | `integration.module_information_merge.empty-field.authors` | 7 |
| `check_module_information_merge.py` | 47, 49 | `integration.module_information_merge.empty-field.producers` | 7 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.description.00` | 4 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.description.01` | 4 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.description.02` | 4 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.description.03` | 4 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.description.04` | 4 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.description.05` | 4 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.description.06` | 4 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.description.07` | 4 |
| `check_module_information_merge.py` | 61 | `integration.module_information_merge.conflict.description.00` | 1 |
| `check_module_information_merge.py` | 61 | `integration.module_information_merge.conflict.description.01` | 1 |
| `check_module_information_merge.py` | 61 | `integration.module_information_merge.conflict.description.02` | 1 |
| `check_module_information_merge.py` | 61 | `integration.module_information_merge.conflict.description.03` | 1 |
| `check_module_information_merge.py` | 61 | `integration.module_information_merge.conflict.description.04` | 1 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.license.00` | 4 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.license.01` | 4 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.license.02` | 4 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.license.03` | 4 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.license.04` | 4 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.license.05` | 4 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.license.06` | 4 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.license.07` | 4 |
| `check_module_information_merge.py` | 61 | `integration.module_information_merge.conflict.license.00` | 1 |
| `check_module_information_merge.py` | 61 | `integration.module_information_merge.conflict.license.01` | 1 |
| `check_module_information_merge.py` | 61 | `integration.module_information_merge.conflict.license.02` | 1 |
| `check_module_information_merge.py` | 61 | `integration.module_information_merge.conflict.license.03` | 1 |
| `check_module_information_merge.py` | 61 | `integration.module_information_merge.conflict.license.04` | 1 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.homepage.00` | 4 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.homepage.01` | 4 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.homepage.02` | 4 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.homepage.03` | 4 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.homepage.04` | 4 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.homepage.05` | 4 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.homepage.06` | 4 |
| `check_module_information_merge.py` | 54, 56, 58 | `integration.module_information_merge.text.homepage.07` | 4 |
| `check_module_information_merge.py` | 61 | `integration.module_information_merge.conflict.homepage.00` | 1 |
| `check_module_information_merge.py` | 61 | `integration.module_information_merge.conflict.homepage.01` | 1 |
| `check_module_information_merge.py` | 61 | `integration.module_information_merge.conflict.homepage.02` | 1 |
| `check_module_information_merge.py` | 61 | `integration.module_information_merge.conflict.homepage.03` | 1 |
| `check_module_information_merge.py` | 61 | `integration.module_information_merge.conflict.homepage.04` | 1 |
| `check_module_information_merge.py` | 67 | `integration.module_information_merge.decoded-text` | 1 |
| `check_module_information_merge.py` | 74 | `integration.module_information_merge.author-order.00` | 1 |
| `check_module_information_merge.py` | 74 | `integration.module_information_merge.author-order.01` | 1 |
| `check_module_information_merge.py` | 74 | `integration.module_information_merge.author-order.02` | 1 |
| `check_module_information_merge.py` | 76 | `integration.module_information_merge.empty-email` | 1 |
| `check_module_information_merge.py` | 81, 82 | `integration.module_information_merge.author-conflict.00` | 2 |
| `check_module_information_merge.py` | 81, 82 | `integration.module_information_merge.author-conflict.01` | 2 |
| `check_module_information_merge.py` | 81, 82 | `integration.module_information_merge.author-conflict.02` | 2 |
| `check_module_information_merge.py` | 81, 82 | `integration.module_information_merge.author-conflict.03` | 2 |
| `check_module_information_merge.py` | 85 | `integration.module_information_merge.interactive-order` | 1 |
| `check_module_information_merge.py` | 86 | `integration.module_information_merge.interactive-one` | 1 |
| `check_module_information_merge.py` | 90, 91 | `integration.module_information_merge.interactive-retry.00` | 2 |
| `check_module_information_merge.py` | 90, 91 | `integration.module_information_merge.interactive-retry.01` | 2 |
| `check_module_information_merge.py` | 90, 91 | `integration.module_information_merge.interactive-retry.02` | 2 |
| `check_module_information_merge.py` | 90, 91 | `integration.module_information_merge.interactive-retry.03` | 2 |
| `check_module_information_merge.py` | 90, 91 | `integration.module_information_merge.interactive-retry.04` | 2 |
| `check_module_information_merge.py` | 90, 91 | `integration.module_information_merge.interactive-retry.05` | 2 |
| `check_module_information_merge.py` | 90, 91 | `integration.module_information_merge.interactive-retry.06` | 2 |
| `check_module_information_merge.py` | 90, 91 | `integration.module_information_merge.interactive-retry.07` | 2 |
| `check_module_information_merge.py` | 90, 91 | `integration.module_information_merge.interactive-retry.08` | 2 |
| `check_module_information_merge.py` | 90, 91 | `integration.module_information_merge.interactive-retry.09` | 2 |
| `check_module_information_merge.py` | 90, 91 | `integration.module_information_merge.interactive-retry.10` | 2 |
| `check_module_information_merge.py` | 96 | `integration.module_information_merge.interactive-multiple-retry` | 1 |
| `check_module_information_merge.py` | 98 | `integration.module_information_merge.interactive-spaces` | 1 |
| `check_module_information_merge.py` | 99 | `integration.module_information_merge.interactive-long-line` | 1 |
| `check_module_information_merge.py` | 104, 105 | `integration.module_information_merge.author-identity-display` | 2 |
| `check_module_information_merge.py` | 111 | `integration.module_information_merge.invalid-author.00` | 1 |
| `check_module_information_merge.py` | 111 | `integration.module_information_merge.invalid-author.01` | 1 |
| `check_module_information_merge.py` | 111 | `integration.module_information_merge.invalid-author.02` | 1 |
| `check_module_information_merge.py` | 111 | `integration.module_information_merge.invalid-author.03` | 1 |
| `check_module_information_merge.py` | 111 | `integration.module_information_merge.invalid-author.04` | 1 |
| `check_module_information_merge.py` | 111 | `integration.module_information_merge.invalid-author.05` | 1 |
| `check_module_information_merge.py` | 111 | `integration.module_information_merge.invalid-author.06` | 1 |
| `check_module_information_merge.py` | 120 | `integration.module_information_merge.producer-permutation.00` | 1 |
| `check_module_information_merge.py` | 120 | `integration.module_information_merge.producer-permutation.01` | 1 |
| `check_module_information_merge.py` | 120 | `integration.module_information_merge.producer-permutation.02` | 1 |
| `check_module_information_merge.py` | 120 | `integration.module_information_merge.producer-permutation.03` | 1 |
| `check_module_information_merge.py` | 120 | `integration.module_information_merge.producer-permutation.04` | 1 |
| `check_module_information_merge.py` | 120 | `integration.module_information_merge.producer-permutation.05` | 1 |
| `check_module_information_merge.py` | 121 | `integration.module_information_merge.producer-dedup` | 1 |
| `check_module_information_merge.py` | 123 | `integration.module_information_merge.authors-and-producers` | 1 |
| `check_module_information_merge.py` | 130 | `integration.module_information_merge.producer-identity.name` | 1 |
| `check_module_information_merge.py` | 130 | `integration.module_information_merge.producer-identity.version` | 1 |
| `check_module_information_merge.py` | 130 | `integration.module_information_merge.producer-identity.buildId` | 1 |
| `check_module_information_merge.py` | 135 | `integration.module_information_merge.decoded-producer` | 1 |
| `check_module_information_merge.py` | 137 | `integration.module_information_merge.invalid-producer.00` | 1 |
| `check_module_information_merge.py` | 137 | `integration.module_information_merge.invalid-producer.01` | 1 |
| `check_module_information_merge.py` | 137 | `integration.module_information_merge.invalid-producer.02` | 1 |
| `check_module_information_merge.py` | 137 | `integration.module_information_merge.invalid-producer.03` | 1 |
| `check_module_information_merge.py` | 140 | `integration.module_information_merge.producer-type.name` | 6 |
| `check_module_information_merge.py` | 142 | `integration.module_information_merge.producer-missing.name` | 1 |
| `check_module_information_merge.py` | 140 | `integration.module_information_merge.producer-type.version` | 6 |
| `check_module_information_merge.py` | 142 | `integration.module_information_merge.producer-missing.version` | 1 |
| `check_module_information_merge.py` | 140 | `integration.module_information_merge.producer-type.buildId` | 6 |
| `check_module_information_merge.py` | 142 | `integration.module_information_merge.producer-missing.buildId` | 1 |
| `check_module_information_merge.py` | 146 | `integration.module_information_merge.late-invalid.00` | 1 |
| `check_module_information_merge.py` | 146 | `integration.module_information_merge.late-invalid.01` | 1 |
| `check_module_information_merge.py` | 146 | `integration.module_information_merge.late-invalid.02` | 1 |
| `check_module_information_merge.py` | 149 | `integration.module_information_merge.late-invalid-before-conflict` | 1 |
| `check_module_information_merge.py` | 153 | `integration.module_information_merge.conflict-before-interaction` | 1 |
