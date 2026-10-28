# Portable module bytecode validation

`vm/module_bytecode.h` provides `read_module_bytecode` for the v1 image and
`module.json`. It performs no execution, native loading, source-file access or
image patching. The inputs are borrowed during the call; success returns one
owned, aligned, immutable bytecode image and the instruction selected through
`main` and `files`. Release the image with `free_bytecode`. Failure clears both
outputs and reports the validation stage and, where applicable, record or
instruction index. Core allocation failure follows the existing fatal policy.

The strict JSON reader first checks exact integer tokens and normalization.
The image decoder then validates every instruction and descriptor, even after
END or in otherwise unreachable code. It checks section arithmetic, UTF-32LE,
reserved fields, opcode-specific operands, ARG pairs, data references, file
spans, local branches, includes, declarations, function IDs and location runs.
File entries must denote top-level code; function bodies must belong to their
function; ARG and its consumer must have the same file/function ownership.
Branches and structured-control continuations cannot cross function ownership.
Runtime stack/type errors are not generally statically decidable.

The module layer cross-checks both file tables and all function records,
including exact parameter names (explicit lengths, not NUL termination).
Function IDs are matched as identifiers in sorted tables, not array indices;
legal gaps and UINT32_MAX are preserved. Metadata is normalized before matching,
so unordered JSON function records do not change their identity. Empty and
omitted function tables are equivalent only when the image has no functions.
Own exported function references are checked by the strict metadata reader.
Each IMPORT must contain a valid, explicit `name@version`; it must name a metadata dependency. Repeated references are allowed, but missing or wrong-version dependencies and malformed imports are errors, even in unreachable
code. Dependencies are derived from syntactic source imports; optimization may
remove their instructions without removing metadata dependencies. Therefore the
validator must not reject metadata merely because no IMPORT remains for it.
INCLUDE references are module-local and checked against the file count.

This is a validation layer, not the future complete module loader: container
integrity and hashes remain the container layer's responsibility; graph-aware
functionRef resolution, analysis proofs, native ABI consistency and execution of
include/import are separate checklist tasks. The standalone reader continues to
use the same image decoder. No legacy conversion or source lookup is added.

## Tests

After `bash scripts/build.sh` (or `scripts\build_mingw.cmd`):

```sh
ctest --test-dir build --output-on-failure -R "^native.bytecode_v1$"
python3 scripts/check_module_bytecode.py build/module_bytecode_testing
```

The component harness consumes independent JSON/image fixtures without running
user code. GitHub CI runs these checks on Linux x86_64/aarch64 and Windows MinGW.
The image unit tests and CLI corruption tests also cover invalid unreachable
instructions, full-width sparse IDs, ownership mismatches and unchanged bytes.

## Entry interface consistency (#205)

After file/function/parameter/import checks, the reader checks `externals` against
entry-file declarations. Names and mutability must match exactly; public/private,
nested object/block and function-body declarations cannot impersonate an external
entry binding. This uses the same scope walker as pending file declarations, in
nonexecuting syntactic mode: lexical fallthrough after throw/return/branches keeps
unreachable source declarations in the interface, but END ends the file body and
function-location runs are excluded. Disconnected bytes after END do not establish
an export. The runtime reservation path keeps its original reachable-only mode.

`MODULE_BYTECODE_EXTERNALS` is appended to the status enumeration (existing values
are unchanged). This check validates declaration structure, not the truth of an
export's type/value or analytical summary. Those guarantees remain the producer's
responsibility; dependency code is never run to confirm them.
