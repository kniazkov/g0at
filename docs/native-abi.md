# Numeric adapter ABI, version 1

The public C header is `include/goat/native_abi.h` (`#include <goat/native_abi.h>`, `-Iinclude`). Generated modules embed the same
declarations and remain standalone C11 files. CI compares the embedded declarations
with the header (ignoring whitespace) and compiles the caller and module as separate
translation units. This ABI contains no `object_t`, AST, arena or allocator pointers.

## Discovery and lifetime

`goat_native_query_v1(version)` returns a pointer to a library-owned immutable
`goat_native_module_v1_t` for version 1, or NULL for any other version. It does not
execute Goat code. The descriptor reports its version, structure size, value size
and alignment, entry size, pointer size and entry count. An empty module has zero
entries and a NULL entries pointer.

Each entry contains the module-local specialization ID, function ordinal, ordered
parameter tags, return tag and adapter function pointer. These are the same IDs
used by the module inventory; omitted functions leave gaps. IDs are not stable
across source edits. Only retained, dependency-closed definitions receive adapters.
Zero-parameter entries have a NULL parameter-types pointer.

All metadata, signature arrays and function pointers remain owned by the compiled
module. Callers must not modify or free them and must keep the library loaded while
using them. Invocation allocates no VM context and retains no argument/result
pointer. Descriptor validation and dynamic-library ownership will be implemented
in the loader step.

Version 1 requires the host and module to use the same target architecture and
compatible C calling convention and structure layout. `GOAT_NATIVE_CALL` explicitly
selects cdecl on Windows (also on 32-bit builds); it is empty elsewhere. It is an in-process
ABI, not a portable serialized format. No packing pragmas or nondefault ABI flags
are supported. A future loader must validate the reported sizes/alignment before
reading entries. Windows providers define `GOAT_NATIVE_BUILD` when building the library to export
`goat_native_query_v1` through `GOAT_NATIVE_API`. The query typedef is
`goat_native_query_v1_t`; provider adapters use `GOAT_NATIVE_CALL` too.

## Values and invocation

`goat_native_value_v1_t` contains a `uint32_t` type tag, a zero reserved field and a
union of `int64_t integer` / binary64 `double real`. Tags are `GOAT_NATIVE_I64=1`
and `GOAT_NATIVE_F64=2`. `GOAT_NATIVE_INVALID=0` is not a numeric parameter type;
other tags are rejected too. ABI fields and returned statuses use fixed-width
integers, not C enum storage. Only the member selected by the tag may be read.

An adapter has this signature:

```c
uint32_t invoke(uint32_t version,
                uint32_t argument_count,
                const goat_native_value_v1_t *arguments,
                goat_native_value_v1_t *result);
```

Arguments are already evaluated and stored in source parameter order: element 0
is the first argument. The VM bridge preserves Goat's right-to-left
argument evaluation before invoking the adapter. There is no evaluation or boxing
of Goat expressions at this boundary.

The adapter checks version, pointers, count and formal parameter tags before calling
the typed C function. It does not convert integer to real or real to integer.
A missing formal argument rejects the specialization; the VM bridge must
preserve Goat's missing-argument `null` semantics through bytecode fallback.
Extra arguments are accepted and ignored: their tags, reserved fields and payloads
are not inspected. The VM remains responsible for their original evaluation and
lifetime. A nonzero argument count requires a non-NULL argument pointer, including
for zero-parameter functions. The result pointer is always required.

Pointers must designate suitably aligned, live storage: a writable result and an
argument array of the supplied count. Validation cannot make arbitrary invalid
pointers safe. Result storage may alias an argument; all required inputs are read
before publishing the result. No pointers are retained, so independent calls need
no shared mutable adapter state.

On success, the result tag matches the entry's return type and `reserved` is zero.
Integer extremes are preserved exactly; real values preserve signed zero, subnormals,
NaN and infinities according to the numeric contract. NaN payload preservation is
not promised. On every rejection the result storage is unchanged, byte for byte.

## Status values

| Constant | Value | Meaning |
| --- | --- | --- |
| `GOAT_NATIVE_OK` | 0 | A complete numeric result was written. |
| `GOAT_NATIVE_TYPE_MISMATCH` | 1 | Missing formal argument or a nonmatching type tag. |
| `GOAT_NATIVE_BAD_REQUEST` | 2 | Missing required pointer or a nonzero reserved field on a formal argument. |
| `GOAT_NATIVE_ABI_MISMATCH` | 3 | Unsupported invocation version. |
| `GOAT_NATIVE_RESOURCE_LIMIT` | 4 | Controlled resource exhaustion; pure calls may retry in bytecode. |
| `GOAT_NATIVE_EXTERNAL_ERROR` | 5 | External execution failed; effects may already have occurred. Never retry automatically. |

Version is checked first, then pointers and the minimum argument count. Formal
arguments are checked in index order, reserved field before type tag. These statuses
are backend outcomes, not Goat exceptions. Numeric eligibility currently excludes
throwing paths; this ABI does not transport arbitrary exception objects.

Generated adapters now bound recursive depth, call count and stack use, returning
`RESOURCE_LIMIT` without changing the output on exhaustion. The VM retries only
`GOAT_NATIVE_PURE` entries, suppressing native calls in the fallback subtree. See the
[execution contract](native-execution.md) for bounds and stack-headroom checks. This
is not signal or stack-overflow recovery.

## Validation

`scripts/check_native_abi.sh GOAT_BINARY OUTPUT_DIR [COMPILER_FLAGS...]` checks header
consistency and calls generated adapters from a separately compiled host. It covers
integer boundaries, real special values, mixed signatures, zero and extra arguments,
recursion, version/type/count/pointer rejection, result aliasing and unchanged output
on failure. Empty inventories expose an empty descriptor. Unit tests ensure omitted
recursive definitions do not receive adapters. CI runs GCC/Clang, all three Windows
GCC targets, Linux UBSan and GCC x87 evaluation.

## Handwritten adapters and precompiled archives

The same ABI can be implemented manually without the Goat compiler. Compile a thin
adapter with this public header and link it with the vendor's static archive or
shared library. Only the adapter must obey this ABI; the wrapped code can use a
vendor API, C++, GPU/NPU toolchain or another private calling convention. The header
provides C linkage when included from C++; no C++ exceptions may cross the boundary.
A C++ toolchain is not required to build Goat.

Each entry has an optional library-owned UTF-8 `binding_name` and a `uint32_t flags`.
Manual providers use nonempty names (for example `device.read`) for future explicit
import binding. Overloads share a name and differ in ordered parameter tags;
duplicate name/signature pairs are invalid. IDs are provider-local, not AST IDs:
specialization IDs are unique in a module and overloads share a function ID. Generated
entries keep NULL names and use the existing inventory IDs. Name lookup must remain
scoped to an explicitly imported module, never override a lexical Goat variable.
Import syntax and VM binding are not implemented by this change.

Flags default to zero: effectful or unknown. `GOAT_NATIVE_PURE` is a provider's
promise of no observable side effects or dependence on mutable external state;
generated entries carry it because numeric eligibility already requires purity.
Reading a device, submitting work, or reading a mutable clock is not pure. This
metadata is not permission for the analyzer to execute arbitrary external code,
and does not provide an abstract evaluator. Unknown flag bits must be rejected by
a future loader. Purity alone does not guarantee termination or successful execution.

The adapter validates the entire request before entering vendor code, leaves the
result untouched on failure, and publishes it only on success. Validation failures
must have no external effects. Once external execution starts, failure is
`GOAT_NATIVE_EXTERNAL_ERROR`, not a type mismatch or a retry request. Even though the
result is unchanged, device state may have changed: the caller must not replay such
a call through bytecode or another adapter. A provider may return the reserved
resource-limit status only if execution is safe to retry, with no observable effects.
The VM reports external failures without automatic retry; mapping
them to Goat exceptions is a separate task.

Version 1 remains synchronous and numeric. An adapter must wait for device work
before returning its numeric result. Buffers, strings, device handles, per-instance
state, asynchronous completion and init/shutdown hooks need a separate future ABI;
do not pass pointers disguised as integers. Provider-private resources and locking
are the provider's responsibility. Dependencies must stay loaded with the provider.
One final module owns one `goat_native_query_v1` and one combined entry table; vendor
archives should not each export competing query symbols. Independently loaded
providers are queried separately by library handle in the future loader.

`test/functional/native_abi/manual/` is a working example: vendor code is compiled
into an archive first, a handwritten adapter links against it, and a separate host
uses only the public header. Tests check named binding, conservative purity, exact
validation, ignored extras, aliasing, and a simulated device failure after a side
effect. The host links the archive transitively through the adapter, with no AST or
VM headers. This models the boundary, not an actual GPU SDK integration.

This step does not load libraries, select specializations inside the VM, or change
`CALL`. Those are later stages of the native backend roadmap.
