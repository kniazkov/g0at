# Complete dispatch/consistency mapping (#365)

Baseline: `77894149bbdf306ea57ac4a6558ebfcc1a125c34`.

| Original case | Stable CTest ID | Mode / expected |
| --- | --- | --- |
| `check_module_dispatch.py`: per-call | `integration.module_dispatch.per-call.auto` | auto; calls 2 |
| `check_module_dispatch.py`: arguments-once | `integration.module_dispatch.arguments-once.auto` | auto; calls 1 |
| `check_module_dispatch.py`: extra-arguments | `integration.module_dispatch.extra-arguments.auto` | auto; calls 1 |
| `check_module_dispatch.py`: missing-null | `integration.module_dispatch.missing-null.auto` | auto; calls 1 |
| `check_module_dispatch.py`: no-specialization | `integration.module_dispatch.no-specialization.auto` | auto; calls 0 |
| `check_module_dispatch.py`: not-equal | `integration.module_dispatch.not-equal.auto` | auto; calls 2 |
| `check_module_dispatch.py`: disjoint | `integration.module_dispatch.disjoint.auto` | auto; calls 2 |
| `check_module_dispatch.py`: same-types-different-return | `integration.module_dispatch.same-types-different-return.auto` | auto; calls 2 |
| `check_module_dispatch.py`: boolean | `integration.module_dispatch.boolean.auto` | auto; calls 2 |
| `check_module_dispatch.py`: character | `integration.module_dispatch.character.auto` | auto; calls 1 |
| `check_module_dispatch.py`: real | `integration.module_dispatch.real.auto` | auto; calls 1 |
| `check_module_dispatch.py`: exact-no-coercion | `integration.module_dispatch.exact-no-coercion.auto` | auto; calls 1 |
| `check_module_dispatch.py`: string-borrowed | `integration.module_dispatch.string-borrowed.auto` | auto; calls 2 |
| `check_module_dispatch.py`: string-host-allocation | `integration.module_dispatch.string-host-allocation.auto` | auto; calls 2 |
| `check_module_dispatch.py`: string-equality | `integration.module_dispatch.string-equality.auto` | auto; calls 1 |
| `check_module_dispatch.py`: string-zero-guard | `integration.module_dispatch.string-zero-guard.auto` | auto; calls 1 |
| `check_module_dispatch.py`: empty-string | `integration.module_dispatch.empty-string.auto` | auto; calls 1 |
| `check_module_dispatch.py`: non-scalar-string-fallback | `integration.module_dispatch.non-scalar-string-fallback.auto` | auto; calls 0; resolutions 0 |
| `check_module_dispatch.py`: ordinary-object-no-conversion | `integration.module_dispatch.ordinary-object-no-conversion.auto` | auto; calls 0 |
| `check_module_dispatch.py`: closure-instances | `integration.module_dispatch.closure-instances.auto` | auto; calls 0 |
| `check_module_dispatch.py`: transferred-method | `integration.module_dispatch.transferred-method.auto` | auto; calls 0 |
| `check_module_dispatch.py`: bad-tag | `integration.module_dispatch.bad-tag.auto` | auto; calls 1; terminal failure |
| `check_module_dispatch.py`: bad-reserved | `integration.module_dispatch.bad-reserved.auto` | auto; calls 1; terminal failure |
| `check_module_dispatch.py`: bad-bool | `integration.module_dispatch.bad-bool.auto` | auto; calls 1; terminal failure |
| `check_module_dispatch.py`: bad-char | `integration.module_dispatch.bad-char.auto` | auto; calls 1; terminal failure |
| `check_module_dispatch.py`: bad-string | `integration.module_dispatch.bad-string.auto` | auto; calls 1; terminal failure |
| `check_module_dispatch.py`: status-mismatch | `integration.module_dispatch.status-mismatch.auto` | auto; calls 1; terminal failure |
| `check_module_dispatch.py`: status-resource | `integration.module_dispatch.status-resource.auto` | auto; calls 1; terminal failure |
| `check_module_dispatch.py`: status-unknown | `integration.module_dispatch.status-unknown.auto` | auto; calls 1; terminal failure |
| `check_module_dispatch.py`: module-method | `integration.module_dispatch.module-method.auto` | auto; calls 1 |
| `check_module_dispatch.py`: initializer-terminal | `integration.module_dispatch.initializer-terminal.auto` | auto; calls 1; terminal failure |
| `check_module_dispatch.py`: worker-terminal | `integration.module_dispatch.worker-terminal.auto` | auto; calls 1; terminal failure |
| `check_module_dispatch.py`: failed-file-before-native | `integration.module_dispatch.failed-file-before-native.auto` | auto; calls 0 |
| `check_module_dispatch.py`: per-call | `integration.module_dispatch.per-call.required` | required; calls 2 |
| `check_module_dispatch.py`: arguments-once | `integration.module_dispatch.arguments-once.required` | required; calls 1 |
| `check_module_dispatch.py`: extra-arguments | `integration.module_dispatch.extra-arguments.required` | required; calls 1 |
| `check_module_dispatch.py`: missing-null | `integration.module_dispatch.missing-null.required` | required; calls 1 |
| `check_module_dispatch.py`: no-specialization | `integration.module_dispatch.no-specialization.required` | required; calls 0 |
| `check_module_dispatch.py`: not-equal | `integration.module_dispatch.not-equal.required` | required; calls 2 |
| `check_module_dispatch.py`: disjoint | `integration.module_dispatch.disjoint.required` | required; calls 2 |
| `check_module_dispatch.py`: same-types-different-return | `integration.module_dispatch.same-types-different-return.required` | required; calls 2 |
| `check_module_dispatch.py`: boolean | `integration.module_dispatch.boolean.required` | required; calls 2 |
| `check_module_dispatch.py`: character | `integration.module_dispatch.character.required` | required; calls 1 |
| `check_module_dispatch.py`: real | `integration.module_dispatch.real.required` | required; calls 1 |
| `check_module_dispatch.py`: exact-no-coercion | `integration.module_dispatch.exact-no-coercion.required` | required; calls 1 |
| `check_module_dispatch.py`: string-borrowed | `integration.module_dispatch.string-borrowed.required` | required; calls 2 |
| `check_module_dispatch.py`: string-host-allocation | `integration.module_dispatch.string-host-allocation.required` | required; calls 2 |
| `check_module_dispatch.py`: string-equality | `integration.module_dispatch.string-equality.required` | required; calls 1 |
| `check_module_dispatch.py`: string-zero-guard | `integration.module_dispatch.string-zero-guard.required` | required; calls 1 |
| `check_module_dispatch.py`: empty-string | `integration.module_dispatch.empty-string.required` | required; calls 1 |
| `check_module_dispatch.py`: non-scalar-string-fallback | `integration.module_dispatch.non-scalar-string-fallback.required` | required; calls 0; resolutions 0 |
| `check_module_dispatch.py`: ordinary-object-no-conversion | `integration.module_dispatch.ordinary-object-no-conversion.required` | required; calls 0 |
| `check_module_dispatch.py`: closure-instances | `integration.module_dispatch.closure-instances.required` | required; calls 0 |
| `check_module_dispatch.py`: transferred-method | `integration.module_dispatch.transferred-method.required` | required; calls 0 |
| `check_module_dispatch.py`: bad-tag | `integration.module_dispatch.bad-tag.required` | required; calls 1; terminal failure |
| `check_module_dispatch.py`: bad-reserved | `integration.module_dispatch.bad-reserved.required` | required; calls 1; terminal failure |
| `check_module_dispatch.py`: bad-bool | `integration.module_dispatch.bad-bool.required` | required; calls 1; terminal failure |
| `check_module_dispatch.py`: bad-char | `integration.module_dispatch.bad-char.required` | required; calls 1; terminal failure |
| `check_module_dispatch.py`: bad-string | `integration.module_dispatch.bad-string.required` | required; calls 1; terminal failure |
| `check_module_dispatch.py`: status-mismatch | `integration.module_dispatch.status-mismatch.required` | required; calls 1; terminal failure |
| `check_module_dispatch.py`: status-resource | `integration.module_dispatch.status-resource.required` | required; calls 1; terminal failure |
| `check_module_dispatch.py`: status-unknown | `integration.module_dispatch.status-unknown.required` | required; calls 1; terminal failure |
| `check_module_dispatch.py`: module-method | `integration.module_dispatch.module-method.required` | required; calls 1 |
| `check_module_dispatch.py`: initializer-terminal | `integration.module_dispatch.initializer-terminal.required` | required; calls 1; terminal failure |
| `check_module_dispatch.py`: worker-terminal | `integration.module_dispatch.worker-terminal.required` | required; calls 1; terminal failure |
| `check_module_dispatch.py`: failed-file-before-native | `integration.module_dispatch.failed-file-before-native.required` | required; calls 0 |
| `check_module_dispatch.py`: overlap | `integration.module_dispatch.overlap.auto` | auto; calls 0 |
| `check_module_dispatch.py`: unknown-function | `integration.module_dispatch.unknown-function.auto` | auto; calls 0 |
| `check_module_dispatch.py`: wrong-arity | `integration.module_dispatch.wrong-arity.auto` | auto; calls 0 |
| `check_module_dispatch.py`: off | `integration.module_dispatch.off.off` | off; calls 0 |
| `check_module_dispatch.py`: load-failure-auto | `integration.module_dispatch.load-failure-auto.auto` | auto; calls 0; resolutions 1 |
| `check_module_dispatch.py`: load-failure-required | `integration.module_dispatch.load-failure-required.required` | required; calls 0; resolutions 1; terminal failure |
| `check_module_dispatch.py`: unused-does-not-resolve | `integration.module_dispatch.unused-does-not-resolve.required` | required; calls 0; resolutions 0 |
| dispatch `--identity` | `integration.module_dispatch.identity` | exit 0, stripped stdout `OK` |
| `check_module_consistency.py` line 106 | `integration.module_consistency.valid-native.off` | off; index 0; status 0 |
| `check_module_consistency.py` line 107 | `integration.module_consistency.without-native.off` | off; index 0; status 0 |
| `check_module_consistency.py` line 109 | `integration.module_consistency.native-hash-length.off` | off; index 0; status 113 |
| `check_module_consistency.py` line 110 | `integration.module_consistency.native-hash-byte.off` | off; index 0; status 113 |
| `check_module_consistency.py` line 111 | `integration.module_consistency.native-missing.off` | off; index 0; status 110 |
| `check_module_consistency.py` line 114 | `integration.module_consistency.function-instruction.off` | off; index 0; status 3 |
| `check_module_consistency.py` line 117 | `integration.module_consistency.specialization-symbol.off` | off; index 0; status 4 |
| `check_module_consistency.py` line 120 | `integration.module_consistency.specialization-overlap.off` | off; index 0; status 4 |
| `check_module_consistency.py` line 123 | `integration.module_consistency.target-cpu.off` | off; index 0; status 4 |
| `check_module_consistency.py` line 126 | `integration.module_consistency.library-extension.off` | off; index 0; status 4 |
| `check_module_consistency.py` line 106 | `integration.module_consistency.valid-native.auto` | auto; index 0; status 0 |
| `check_module_consistency.py` line 107 | `integration.module_consistency.without-native.auto` | auto; index 0; status 0 |
| `check_module_consistency.py` line 109 | `integration.module_consistency.native-hash-length.auto` | auto; index 0; status 113 |
| `check_module_consistency.py` line 110 | `integration.module_consistency.native-hash-byte.auto` | auto; index 0; status 113 |
| `check_module_consistency.py` line 111 | `integration.module_consistency.native-missing.auto` | auto; index 0; status 110 |
| `check_module_consistency.py` line 114 | `integration.module_consistency.function-instruction.auto` | auto; index 0; status 3 |
| `check_module_consistency.py` line 117 | `integration.module_consistency.specialization-symbol.auto` | auto; index 0; status 4 |
| `check_module_consistency.py` line 120 | `integration.module_consistency.specialization-overlap.auto` | auto; index 0; status 4 |
| `check_module_consistency.py` line 123 | `integration.module_consistency.target-cpu.auto` | auto; index 0; status 4 |
| `check_module_consistency.py` line 126 | `integration.module_consistency.library-extension.auto` | auto; index 0; status 4 |
| `check_module_consistency.py` line 106 | `integration.module_consistency.valid-native.required` | required; index 0; status 0 |
| `check_module_consistency.py` line 107 | `integration.module_consistency.without-native.required` | required; index 0; status 0 |
| `check_module_consistency.py` line 109 | `integration.module_consistency.native-hash-length.required` | required; index 0; status 113 |
| `check_module_consistency.py` line 110 | `integration.module_consistency.native-hash-byte.required` | required; index 0; status 113 |
| `check_module_consistency.py` line 111 | `integration.module_consistency.native-missing.required` | required; index 0; status 110 |
| `check_module_consistency.py` line 114 | `integration.module_consistency.function-instruction.required` | required; index 0; status 3 |
| `check_module_consistency.py` line 117 | `integration.module_consistency.specialization-symbol.required` | required; index 0; status 4 |
| `check_module_consistency.py` line 120 | `integration.module_consistency.specialization-overlap.required` | required; index 0; status 4 |
| `check_module_consistency.py` line 123 | `integration.module_consistency.target-cpu.required` | required; index 0; status 4 |
| `check_module_consistency.py` line 126 | `integration.module_consistency.library-extension.required` | required; index 0; status 4 |
| `check_module_consistency.py` line 131 | `integration.module_consistency.future-abi.2` | off; index 0; status 0 |
| `check_module_consistency.py` line 131 | `integration.module_consistency.future-abi.4294967295` | off; index 0; status 0 |
| `check_module_consistency.py` line 133 | `integration.module_consistency.future-cpu` | off; index 0; status 0 |
| `check_module_consistency.py` line 136 | `integration.module_consistency.function-id.1` | off; index 0; status 0 |
| `check_module_consistency.py` line 136 | `integration.module_consistency.function-id.4294967295` | off; index 0; status 0 |
| `check_module_consistency.py` line 139 | `integration.module_consistency.specialization-subset` | off; index 0; status 0 |
| `check_module_consistency.py` line 145 | `integration.module_consistency.foreign-variant` | off; index 0; status 0 |
| `check_module_consistency.py` line 152 | `integration.module_consistency.foreign-invalid.symbol` | off; index 0; status 4 |
| `check_module_consistency.py` line 152 | `integration.module_consistency.foreign-invalid.cpu` | off; index 0; status 4 |
| `check_module_consistency.py` line 156 | `integration.module_consistency.descriptor-match` | descriptor; index 0; status 0 |
| `check_module_consistency.py` line 160 | `integration.module_consistency.descriptor-module` | descriptor; index 0; status 5 |
| `check_module_consistency.py` line 160 | `integration.module_consistency.descriptor-version` | descriptor; index 0; status 5 |
| `check_module_consistency.py` line 160 | `integration.module_consistency.descriptor-mainHash` | descriptor; index 0; status 5 |
| `check_module_consistency.py` line 160 | `integration.module_consistency.descriptor-goatAbiVersion` | descriptor; index 0; status 5 |
| `check_module_consistency.py` line 163 | `integration.module_consistency.descriptor-minimumOs` | descriptor; index 0; status 5 |
| `check_module_consistency.py` line 163 | `integration.module_consistency.descriptor-cpuFeatures` | descriptor; index 0; status 5 |
| `check_module_consistency.py` line 168 | `integration.module_consistency.descriptor-specializationId` | descriptor; index 0; status 5 |
| `check_module_consistency.py` line 168 | `integration.module_consistency.descriptor-functionId` | descriptor; index 0; status 5 |
| `check_module_consistency.py` line 170 | `integration.module_consistency.descriptor-return` | descriptor; index 0; status 5 |
| `check_module_consistency.py` line 171 | `integration.module_consistency.descriptor-index` | descriptor; index 99; status 1 |
| `check_module_consistency.py` line 172 | `integration.module_consistency.descriptor-terminator` | descriptor; index 0; status 5 |
| `check_module_consistency.py` line 174 | `integration.module_consistency.descriptor-compiler-ignored` | descriptor; index 0; status 0 |
| `check_module_consistency.py` line 176 | `integration.module_consistency.descriptor-library-hash-ignored` | descriptor; index 0; status 0 |
| `check_module_consistency.py` line 194 | `integration.module_consistency.graph-transitive` | graph; index 0; status 0 |
| `check_module_consistency.py` line 195 | `integration.module_consistency.graph-root-index` | graph; index 1; status 0 |
| `check_module_consistency.py` line 196 | `integration.module_consistency.graph-missing` | graph; index 0; status 6 |
| `check_module_consistency.py` line 197 | `integration.module_consistency.graph-wrong-version` | graph; index 0; status 6 |
| `check_module_consistency.py` line 198 | `integration.module_consistency.graph-duplicate` | graph; index 0; status 6 |
| `check_module_consistency.py` line 201 | `integration.module_consistency.graph-function-id.1` | graph; index 0; status 0 |
| `check_module_consistency.py` line 201 | `integration.module_consistency.graph-function-id.4294967295` | graph; index 0; status 0 |
| `check_module_consistency.py` line 203 | `integration.module_consistency.graph-function-missing` | graph; index 0; status 7 |
| `check_module_consistency.py` line 205 | `integration.module_consistency.graph-owner-unreachable` | graph; index 0; status 7 |
| `check_module_consistency.py` line 207 | `integration.module_consistency.graph-no-dependency` | graph; index 0; status 7 |
| `check_module_consistency.py` line 209 | `integration.module_consistency.graph-hash-mismatch` | graph; index 0; status 6 |
| `check_module_consistency.py` line 211 | `integration.module_consistency.graph-self-reference` | graph; index 0; status 7 |
| `check_module_consistency.py` line 212 | `integration.module_consistency.graph-index` | graph; index 3; status 1 |
| `check_module_consistency.py` line 219 | `integration.module_consistency.descriptor-second-variant` | descriptor; index 1; status 0 |
| `check_module_consistency.py` line 220 | `integration.module_consistency.descriptor-wrong-variant` | descriptor; index 1; status 5 |
| consistency noargs | `integration.module_consistency.api` | actual API assertions |
| consistency lines 225–255 | `integration.module_consistency.producer-identities` | four real compilations, equal six-word function tables, IDs 1–6, generated symbols |
