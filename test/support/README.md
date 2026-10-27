# Test support

Shared test framework components will be added here during the migration.

The data relocation maps `test/{analysis,binary-v1,functional,native,performance,unicode}`
to the same names under `test/data/`. Native Python checks moved from
`test/native/checks/` to `test/src/native/checks/`; native C hosts already live
under `test/src/native/`. Format specifications and their vectors remain in
`docs/module-format-v1/`.
