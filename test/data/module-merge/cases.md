# Scenario mapping (#374)

| Stable ID | Original line | Contract | Local original |
| --- | ---: | --- | --- |
| `usage.no-arguments` | 163 | merge | passed |
| `usage.output-only` | 163 | merge | passed |
| `usage.unknown-option` | 163 | merge | passed |
| `usage.named-output` | 163 | merge | passed |
| `usage.force-option` | 163 | merge | passed |
| `usage.help` | 166 | merge | passed |
| `usage.version` | 166 | merge | passed |
| `golden.single` | 168 | merge; independent success oracle | passed |
| `golden.duplicates` | 171 | merge; independent success oracle | passed |
| `paths.gbin-suffix` | 176 | merge; independent success oracle | passed |
| `paths.other-suffix` | 176 | merge; independent success oracle | passed |
| `paths.unicode-output` | 176 | merge; independent success oracle | passed |
| `paths.literal-backslash` | 180 | merge; independent success oracle; POSIX only | passed |
| `paths.dash-output` | 182 | merge | passed |
| `paths.dash-input` | 185 | merge | passed |
| `paths.unicode-input` | 188 | merge; independent success oracle | passed |
| `paths.extensionless-input` | 192 | merge; independent success oracle | passed |
| `paths.literal-extension` | 192 | merge; independent success oracle | passed |
| `paths.no-suffix-fallback` | 195 | merge; preserve destination/siblings | passed |
| `validation.failfast-first` | 201 | merge; preserve destination/siblings | passed |
| `validation.failfast-later` | 201 | merge; preserve destination/siblings | passed |
| `invalid.main-hash` | 217 | merge; preserve destination/siblings | passed |
| `invalid.dependencies-null` | 217 | merge; preserve destination/siblings | passed |
| `invalid.authors-null` | 217 | merge; preserve destination/siblings | passed |
| `invalid.author-empty-name` | 217 | merge; preserve destination/siblings | passed |
| `invalid.author-duplicate` | 217 | merge; preserve destination/siblings | passed |
| `invalid.description-null` | 217 | merge; preserve destination/siblings | passed |
| `invalid.homepage-null` | 217 | merge; preserve destination/siblings | passed |
| `invalid.license-null` | 217 | merge; preserve destination/siblings | passed |
| `invalid.author-email-null` | 217 | merge; preserve destination/siblings | passed |
| `invalid.unknown-field` | 217 | merge; preserve destination/siblings | passed |
| `invalid.bytecode-opcode` | 217 | merge; preserve destination/siblings | passed |
| `validation.identity-conflict` | 219 | merge; preserve destination/siblings | passed |
| `publication.replace-existing` | 222 | merge | passed |
| `publication.replace-corrupt` | 231 | merge; independent success oracle | passed |
| `publication.replace-other-module` | 231 | merge; independent success oracle | passed |
| `publication.replace-reversed` | 231 | merge; independent success oracle | passed |
| `publication.destination-invalid-source` | 234 | merge; preserve destination/siblings | passed |
| `publication.destination-valid-source` | 236 | merge; independent success oracle | passed |
| `publication.preserve-modes` | 240 | merge; independent success oracle; POSIX only | passed |
| `publication.preserve-old-hardlink` | 245 | merge; independent success oracle | passed |
| `publication.new-umask` | 253 | merge; independent success oracle; POSIX only | passed |
| `nonexecution.looping-bytecode` | 262 | merge; independent success oracle | passed |
| `dependencies.order-independent` | 268 | merge; independent success oracle | passed |
| `dependencies.hash-conflict` | 271 | merge; preserve destination/siblings | passed |
| `normalize.description.source-first` | 279 | merge; independent success oracle | passed |
| `normalize.description.empty-first` | 279 | merge; independent success oracle | passed |
| `normalize.license.source-first` | 279 | merge; independent success oracle | passed |
| `normalize.license.empty-first` | 279 | merge; independent success oracle | passed |
| `normalize.homepage.source-first` | 279 | merge; independent success oracle | passed |
| `normalize.homepage.empty-first` | 279 | merge; independent success oracle | passed |
| `normalize.authors.source-first` | 279 | merge; independent success oracle | passed |
| `normalize.authors.empty-first` | 279 | merge; independent success oracle | passed |
| `normalize.dependencies.source-first` | 279 | merge; independent success oracle | passed |
| `normalize.dependencies.empty-first` | 279 | merge; independent success oracle | passed |
| `information.description.space.equal` | 286 | merge; independent success oracle | passed |
| `information.description.space.missing-right` | 287 | merge; preserve destination/siblings | passed |
| `information.description.space.missing-left` | 288 | merge; preserve destination/siblings | passed |
| `information.description.composed.equal` | 286 | merge; independent success oracle | passed |
| `information.description.composed.missing-right` | 287 | merge; preserve destination/siblings | passed |
| `information.description.composed.missing-left` | 288 | merge; preserve destination/siblings | passed |
| `information.description.decomposed.equal` | 286 | merge; independent success oracle | passed |
| `information.description.decomposed.missing-right` | 287 | merge; preserve destination/siblings | passed |
| `information.description.decomposed.missing-left` | 288 | merge; preserve destination/siblings | passed |
| `information.description.unicode.equal` | 286 | merge; independent success oracle | passed |
| `information.description.unicode.missing-right` | 287 | merge; preserve destination/siblings | passed |
| `information.description.unicode.missing-left` | 288 | merge; preserve destination/siblings | passed |
| `information.description.nul.equal` | 286 | merge; independent success oracle | passed |
| `information.description.nul.missing-right` | 287 | merge; preserve destination/siblings | passed |
| `information.description.nul.missing-left` | 288 | merge; preserve destination/siblings | passed |
| `information.license.space.equal` | 286 | merge; independent success oracle | passed |
| `information.license.space.missing-right` | 287 | merge; preserve destination/siblings | passed |
| `information.license.space.missing-left` | 288 | merge; preserve destination/siblings | passed |
| `information.license.composed.equal` | 286 | merge; independent success oracle | passed |
| `information.license.composed.missing-right` | 287 | merge; preserve destination/siblings | passed |
| `information.license.composed.missing-left` | 288 | merge; preserve destination/siblings | passed |
| `information.license.decomposed.equal` | 286 | merge; independent success oracle | passed |
| `information.license.decomposed.missing-right` | 287 | merge; preserve destination/siblings | passed |
| `information.license.decomposed.missing-left` | 288 | merge; preserve destination/siblings | passed |
| `information.license.unicode.equal` | 286 | merge; independent success oracle | passed |
| `information.license.unicode.missing-right` | 287 | merge; preserve destination/siblings | passed |
| `information.license.unicode.missing-left` | 288 | merge; preserve destination/siblings | passed |
| `information.license.nul.equal` | 286 | merge; independent success oracle | passed |
| `information.license.nul.missing-right` | 287 | merge; preserve destination/siblings | passed |
| `information.license.nul.missing-left` | 288 | merge; preserve destination/siblings | passed |
| `information.homepage.space.equal` | 286 | merge; independent success oracle | passed |
| `information.homepage.space.missing-right` | 287 | merge; preserve destination/siblings | passed |
| `information.homepage.space.missing-left` | 288 | merge; preserve destination/siblings | passed |
| `information.homepage.composed.equal` | 286 | merge; independent success oracle | passed |
| `information.homepage.composed.missing-right` | 287 | merge; preserve destination/siblings | passed |
| `information.homepage.composed.missing-left` | 288 | merge; preserve destination/siblings | passed |
| `information.homepage.decomposed.equal` | 286 | merge; independent success oracle | passed |
| `information.homepage.decomposed.missing-right` | 287 | merge; preserve destination/siblings | passed |
| `information.homepage.decomposed.missing-left` | 288 | merge; preserve destination/siblings | passed |
| `information.homepage.unicode.equal` | 286 | merge; independent success oracle | passed |
| `information.homepage.unicode.missing-right` | 287 | merge; preserve destination/siblings | passed |
| `information.homepage.unicode.missing-left` | 288 | merge; preserve destination/siblings | passed |
| `information.homepage.nul.equal` | 286 | merge; independent success oracle | passed |
| `information.homepage.nul.missing-right` | 287 | merge; preserve destination/siblings | passed |
| `information.homepage.nul.missing-left` | 288 | merge; preserve destination/siblings | passed |
| `authors.sole-source-order` | 294 | merge; independent success oracle | passed |
| `authors.distinct-equal` | 297 | merge; preserve destination/siblings | passed |
| `authors.distinct-with-empty` | 297 | merge; preserve destination/siblings | passed |
| `authors.distinct-reversed` | 297 | merge; preserve destination/siblings | passed |
| `authors.lexical-alias` | 299 | merge; preserve destination/siblings | passed |
| `producers.forward` | 306 | merge; independent success oracle | passed |
| `producers.reverse` | 306 | merge; independent success oracle | passed |
| `destination.directory` | 313 | merge | passed |
| `destination.missing-parent` | 313 | merge | passed |
| `destination.missing-nested` | 313 | merge | passed |
| `destination.file-parent` | 313 | merge | passed |
| `source.directory` | 316 | merge; preserve destination/siblings | passed |
| `source.symlink` | 326 | merge; independent success oracle | passed |
| `destination.symlink-authors` | 328 | merge | passed |
| `destination.symlink-source` | 328 | merge | passed |
| `destination.dangling-symlink` | 332 | merge | passed |
| `destination.symlink-parent` | 336 | merge; independent success oracle | passed |
| `source.fifo` | 339 | merge; preserve destination/siblings; POSIX only | passed |
| `destination.fifo` | 340 | merge; POSIX only | passed |
| `source.socket` | 352 | merge; preserve destination/siblings; POSIX only | EPERM; strict CI required |
| `destination.socket` | 353 | merge; POSIX only | EPERM; strict CI required |
| `source.device` | 355 | merge; preserve destination/siblings; POSIX only | passed |
| `native.union-forward` | 368 | merge; independent success oracle | passed |
| `native.union-reverse` | 368 | merge; independent success oracle | passed |
| `native.union-duplicates` | 368 | merge; independent success oracle | passed |
| `native.combined-order` | 373 | merge; independent success oracle | passed |
| `native.library-alias-dedup` | 376 | merge; independent success oracle | passed |
| `native.payload-conflict` | 380 | merge; preserve destination/siblings | passed |
| `native.minimum-os-overlap` | 383 | merge; preserve destination/siblings | passed |
| `native.cpu-overlap` | 389 | merge; preserve destination/siblings | passed |
| `native.runtime-version-overlap` | 389 | merge; preserve destination/siblings | passed |
| `native.runtime-distinct` | 395 | merge; independent success oracle | passed |
| `native.architecture-distinct` | 395 | merge; independent success oracle | passed |
| `native.specialization-conflict` | 400 | merge; preserve destination/siblings | passed |
| `native.unknown-abi-distinct` | 403 | merge; independent success oracle | passed |
| `native.unknown-abi-dedup` | 406 | merge; independent success oracle | passed |
| `native.unknown-abi-requirements` | 410 | merge; independent success oracle | passed |
| `native.unknown-abi-payload-conflict` | 413 | merge; preserve destination/siblings | passed |
| `native.compiler-prefer-forward` | 419 | merge; independent success oracle | passed |
| `native.compiler-prefer-reverse` | 419 | merge; independent success oracle | passed |
| `native.compiler-repeat` | 419 | merge; independent success oracle | passed |
| `native.compiler-conflict.name.a-c-d` | 425 | merge; preserve destination/siblings | passed |
| `native.compiler-conflict.name.a-d-c` | 425 | merge; preserve destination/siblings | passed |
| `native.compiler-conflict.name.c-a-d` | 425 | merge; preserve destination/siblings | passed |
| `native.compiler-conflict.name.c-d-a` | 425 | merge; preserve destination/siblings | passed |
| `native.compiler-conflict.name.d-a-c` | 425 | merge; preserve destination/siblings | passed |
| `native.compiler-conflict.name.d-c-a` | 425 | merge; preserve destination/siblings | passed |
| `native.compiler-conflict.version.a-c-d` | 425 | merge; preserve destination/siblings | passed |
| `native.compiler-conflict.version.a-d-c` | 425 | merge; preserve destination/siblings | passed |
| `native.compiler-conflict.version.c-a-d` | 425 | merge; preserve destination/siblings | passed |
| `native.compiler-conflict.version.c-d-a` | 425 | merge; preserve destination/siblings | passed |
| `native.compiler-conflict.version.d-a-c` | 425 | merge; preserve destination/siblings | passed |
| `native.compiler-conflict.version.d-c-a` | 425 | merge; preserve destination/siblings | passed |
| `native.invalid-hash` | 428 | merge; preserve destination/siblings | passed |
| `native.invalid-function` | 431 | merge; preserve destination/siblings | passed |
| `native.compiler-null` | 436 | merge; preserve destination/siblings | passed |
| `native.compiler-empty` | 436 | merge; preserve destination/siblings | passed |
| `native.compiler-no-version` | 436 | merge; preserve destination/siblings | passed |
| `native.compiler-empty-name` | 436 | merge; preserve destination/siblings | passed |
| `native.compiler-unknown-field` | 436 | merge; preserve destination/siblings | passed |
| `nonexecution.real-constructor` | 466 | merge; independent success oracle | passed |
| `graph.inspect-missing-transitive` | 513 | inspect | passed |
| `graph.merge-missing-transitive` | 515 | merge; independent success oracle | passed |
| `graph.inspect-repacked-missing` | 518 | inspect | passed |
| `graph.inspect-restored` | 520 | inspect | passed |
| `graph.missing-function.merge` | 537 | merge; independent success oracle | passed |
| `graph.missing-function.inspect-source` | 540 | inspect | passed |
| `graph.missing-function.inspect-result` | 541 | inspect | passed |
| `graph.unreachable.merge` | 537 | merge; independent success oracle | passed |
| `graph.unreachable.inspect-source` | 540 | inspect | passed |
| `graph.unreachable.inspect-result` | 541 | inspect | passed |
| `graph.missing-module.merge` | 537 | merge; independent success oracle | passed |
| `graph.missing-module.inspect-source` | 540 | inspect | passed |
| `graph.missing-module.inspect-result` | 541 | inspect | passed |
| `graph.wrong-hash.merge` | 537 | merge; independent success oracle | passed |
| `graph.wrong-hash.inspect-source` | 540 | inspect | passed |
| `graph.wrong-hash.inspect-result` | 541 | inspect | passed |
| `graph.malformed-function-id.string.merge` | 550 | merge; preserve destination/siblings | passed |
| `graph.malformed-function-id.string.inspect` | 555 | inspect | passed |
| `graph.malformed-function-id.zero.merge` | 550 | merge; preserve destination/siblings | passed |
| `graph.malformed-function-id.zero.inspect` | 555 | inspect | passed |
| `graph.malformed-function-id.negative.merge` | 550 | merge; preserve destination/siblings | passed |
| `graph.malformed-function-id.negative.inspect` | 555 | inspect | passed |
| `graph.bad-transitive.corrupt.inspect-source` | 560 | inspect | passed |
| `graph.bad-transitive.corrupt.merge` | 562 | merge; independent success oracle | passed |
| `graph.bad-transitive.corrupt.inspect-result` | 563 | inspect | passed |
| `graph.bad-transitive.wrong-identity.inspect-source` | 560 | inspect | passed |
| `graph.bad-transitive.wrong-identity.merge` | 562 | merge; independent success oracle | passed |
| `graph.bad-transitive.wrong-identity.inspect-result` | 563 | inspect | passed |
| `terminal.retry-order` | 614 | interactive; POSIX only | passed |
| `terminal.eof-new` | 617 | interactive; POSIX only | passed |
| `terminal.eof-existing` | 620 | interactive; POSIX only | passed |
| `terminal.replace-existing` | 621 | interactive; POSIX only | passed |
| `terminal.frozen-source` | 628 | interactive; POSIX only | passed |
| `terminal.destination-swap` | 636 | interactive; POSIX only | passed |
| `terminal.identical-author-sources` | 639 | interactive; POSIX only | passed |
