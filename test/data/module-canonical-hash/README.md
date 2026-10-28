# Canonicalization and main-hash corpus (#343)

`cases.json` preserves **all 935 subprocess requests**, in their original order,
from these scripts at parent commit `09302829bd05aec9d47a51415e7cdb42d865eea5`:

| Original script | `scenario` | Requests | Exit 0 | Exit 1 | Exit 3 |
|---|---|---:|---:|---:|---:|
| `scripts/check_module_canonical.py` | `canonical` | 422 | 328 | 94 | 0 |
| `scripts/check_module_hash.py` | `hash` | 513 | 333 | 11 | 169 |

The original scripts ran unchanged, including all their cross-request assertions:
idempotence; exact integer, Unicode and control-byte output; semantic reductions;
ordering/permutations; projection fields; hash framing/block boundaries; significant
versus informational data; valid verification and deliberate hash mismatch.
Duplicate requests and malformed inputs are retained rather than deduplicated.

Each row has `scenario`, `mode` (without the `--` prefix), `inputHex`, `exitCode`,
`expectedHex`, and `sourceLine`. `inputHex` is the complete exact stdin, including
NUL framing and bytecode for hash requests. `sourceLine` identifies the original
caller's line (helper-mediated calls may share a line). `expectedHex` is meaningful
only for successful requests; failing stdout and diagnostic wording are not part
of the original assertions and are deliberately not frozen as requirements.

Successful expected bytes were computed **independently**, not copied from the
production host: `reference.canonical(reference.parse(raw))` for plain JSON;
`reference.canonical(reference.normalize(reference.parse(raw), kind))` for
metadata; `reference.main_bytes` for metadata projection; and
`reference.main_hash` for hashes. Successful `verify-hash` checks independently
required the calculated hash to equal the stored `mainHash` and empty stdout.
Each independently computed result was compared byte-for-byte to production
while all original assertions were still active. The new C runner additionally
compares production with the independent C format oracle at runtime.

## Provenance and reproduction

Original parity command (each succeeds against the same production executable):

```sh
python3 scripts/check_module_canonical.py build/common-debug/module_json_testing
python3 scripts/check_module_hash.py build/common-debug/module_json_testing
```

To reproduce the corpus, restore those scripts from the parent commit, temporarily
wrap `subprocess.run` to retain its actual result and record the exact `input`, mode,
exit status and caller line, and execute each script using `runpy.run_path` with
`run_name='__main__'`. For each successful request, compute and assert the independent
expected bytes with the functions above. Serialize the accumulated rows using
`json.dumps(rows, ensure_ascii=False, indent=2) + '\n'`. The temporary capture
utility is not committed and is not a build/runtime dependency. The C scenarios
read this input without modifying it; no Python is needed for their execution.

SHA-256 fingerprints:

- Corpus: `d3648c4866acc452848b15377fe05588f8e78b992bc9310ec178e8ecc76d5141`
- Original canonical script: `e6c1826f75235b8ffcaec696539840114cea91a1ccd932e63e1f798d06351326`
- Original hash script: `ceee9ddfbcfff303734cfa32c99dcb7162c0608db7fe228828328eb9acda38a3`
- Independent Python reference: `cfe43b8e2ad650c4d7a7d46e7494e6bf5af441ca9f029b2053109692cad4c26e`

See [scenario mapping and commands](../../src/functional/README.md).
