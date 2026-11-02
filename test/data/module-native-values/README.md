# Module native values migration (#382)

Four byte-exact inputs come from `scripts/check_module_native_values.py` at
`ec1a4ce346a0e0dd66adc242e411a8b2e133dc6d`: the original ABI-value source,
unsafe-character source, unsafe-only source, and Python `json.dumps` descriptor.
`provenance.json` records their hashes and extraction method. No production parser,
serializer, compiler output or expected-value generator supplied these inputs.
Scoped `-text` preserves fixtures. Runtime copies are isolated per scenario;
Windows copies reproduce Python text-write LF-to-CRLF translation, while raw
archive metadata and DLL/SO bytes are never text-normalized.

## Original operation → stable ID

Prefix: `integration.module_native_values.`

| ID | Original operations and acceptance |
| --- | --- |
| `abi-values` | native-build status0 + empty stdout; independently extract exact `module.json` and first native variant's library; real same-bitness ABI runner status0, strict UTF-8 stdout |
| `unsafe-character-vm` | native-build status0 (stdout unrestricted); first variant must specialize square, not convert or indirect; real binary execution status0 and bytes.strip()==4294967295 |
| `required-unsafe-only` | recreate the successful unsafe archive, replace source with unsafe-only, then native-build must return nonzero, empty stdout, and stderr containing `own specialization` |

The third independent scenario repeats the successful build prerequisite so its
rejected replacement still sees an existing unsafe archive, matching the original
ordered sequence. Across the IDs there are six subprocess invocations instead of
the original five solely because of this repeated setup. Builds retain individual
120-second deadlines, executions 30 seconds, inside a 300-second scenario bound
(CTest 310 seconds). Every case has unique work, logs, failure cleanup and
`--keep-work` support. Signals, timeouts and cleanup failures cannot pass as an
intentional build rejection.

## Independent observations and real ABI execution

The test-only `format_zip` reader uses zlib primitives rather than production
archive loading. It validates stored/deflate data and CRC, selects the last matching
entry like Python ZIP name lookup, and bounds extraction at 128 MiB. Metadata is
parsed with a generic JSON DOM; function IDs come from `externals` and the first
native variant only. All three specialization inclusion/exclusion checks are
independent, not delegated to production metadata validation.

The actual compiled native library is loaded only by the same-bitness
`module_native_values_testing` subprocess. Its existing 24 value/ABI checks cover
integers, reals, booleans, Unicode characters and strings, embedded NUL, case/trim
callbacks, and ABI/type/bad-request rejection. Those assertions (including adapter
calls and unload side effects) remain active in Release via the documented NDEBUG
fixture dependency. A VM fallback cannot substitute for those ABI calls.

The original ABI runner stdout was decoded and printed, not compared to a fixed
success sentence. That scope is preserved: valid arbitrary UTF-8 stdout and
unspecified stderr are accepted after normal status0. Only the VM result uses the
exact ASCII-whitespace stripping and decimal output comparison. Native-build
stderr likewise remains unrestricted except for the required rejection substring.

Compiler lookup is test-only PATH/PATHEXT/current-directory resolution, preserving
symlink spelling and falling back to the original argument when not found, as the
original `shutil.which(...) or argument` did. Every command retains the caller's
working directory; all scenario artifacts use absolute paths. No shared helper
or production behavior is changed by this runner.

## Local validation

Debug, Release and UBSan each pass all three C IDs. The unchanged original Python
passes in those same three configurations, including 24 real ABI checks and both
unsafe-character policies. Full remote original/C parity remains mandatory before
removing Python.

Independent controls passed **36/36** across Debug and Release:

- 32 real-build-backed mutation/policy controls reject wrong ABI status, malformed
  UTF-8 stdout, forbidden initial builder output, missing square specialization,
  unsafe convert/indirect specialization, wrong VM output, required rejection with
  success status/output/wrong diagnostic, invalid ZIP, absent metadata and absent
  native library. They also accept originally unspecified runner output/stderr,
  unrestricted unsafe-build output and ASCII-whitespace-padded correct VM output.
- Four compiler controls cover caller-relative and bare PATH names through a
  symlinked compiler wrapper. An independent builder spy verifies that the alias
  spelling reaches the builder, then forwards to the actual builder/compiler and
  real ABI runner. The production compiler layer may canonicalize internally;
  these controls specifically verify the migrated driver's original boundary.

Logs: `/tmp/382-values-tests-{Debug,Release,UBSan}.log`, corresponding
`original` logs, `/tmp/382-values-negative.log` and
`/tmp/382-values-compiler-controls.log` in the implementation workspace.
