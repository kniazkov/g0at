# Metadata JSON corpus (#342)

`cases.json` freezes **all 440 calls** made by `scripts/check_module_json.py`
at parent commit `5e56f72c57c5df34237dd330e9bd420d4d2131e7`: 93 accepted and
347 rejected descriptors. Order and duplicate requests are intentionally retained.
`sourceLine` records the original call site (summary checks share lines 73/75).
`inputHex` preserves exact malformed UTF-8, integer lexemes, escapes and nesting.
The C runner reads this file without writing to it. No Python runs in the new test.

Every successful `expected` value was computed **independently** using
`reference.normalize(reference.parse(input), kind)` from the test-only Python
format reference, and compared to the production host's output. It is not an
unchecked production snapshot. The unchanged original script also ran all its
assertions, including cross-request equality, boundaries and exact path behavior.
Failures retain the original required exit code 1 and, where specified, diagnostic
substring. Unspecified diagnostics remain unspecified rather than freezing wording.

Corpus SHA-256: `f00e3984382f88e198224367d4079e3c332c5df6a1a921eea934193e5fabc147`.
Original runner SHA-256: `f424dbec8c23bef1668941f2ce5a17fae4c28efc0ee0ef752acf8faba8594950`.
Independent Python reference SHA-256: `cfe43b8e2ad650c4d7a7d46e7494e6bf5af441ca9f029b2053109692cad4c26e`.

Reproduction: restore the original runner from the parent, wrap its `read` function
without changing any assertions, serialize input exactly as the original does,
record kind/error and caller line, and compute successful expectations with the
independent reference above. Run the original `main()` and require all comparisons
and original assertions to pass before accepting the corpus. The temporary
capture utility is not a runtime/build dependency and is not committed.

See [scenario mapping and commands](../../src/functional/README.md#metadata-json-342).
