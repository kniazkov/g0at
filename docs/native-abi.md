# Numeric adapter ABI, version 1

The public C header is `src/codegen/native_abi.h`. Generated modules embed the same
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
compatible default C calling convention and structure layout. It is an in-process
ABI, not a portable serialized format. No packing pragmas or nondefault ABI flags
are supported. A future loader must validate the reported sizes/alignment before
reading entries. Shared-library exports and Windows export annotations belong to
the following compilation steps.

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
is the first argument. The future VM bridge must preserve Goat's right-to-left
argument evaluation before invoking the adapter. There is no evaluation or boxing
of Goat expressions at this boundary.

The adapter checks version, pointers, count and formal parameter tags before calling
the typed C function. It does not convert integer to real or real to integer.
A missing formal argument rejects the specialization; a future VM bridge must
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
| `GOAT_NATIVE_RESOURCE_LIMIT` | 4 | Reserved for controlled resource exhaustion and bytecode retry. |

Version is checked first, then pointers and the minimum argument count. Formal
arguments are checked in index order, reserved field before type tag. These statuses
are backend outcomes, not Goat exceptions. Numeric eligibility currently excludes
throwing paths; this ABI does not transport arbitrary exception objects.

Resource-limit reporting is reserved but not implemented yet. Recursive adapters
currently execute ordinary C recursion, just like the standalone emitter tests.
They must not be enabled as the VM's native path until bounded native recursion and
controlled retry are implemented in step 17. No signal/stack-overflow recovery is
implied by the reserved status.

## Validation

`scripts/check_native_abi.sh GOAT_BINARY OUTPUT_DIR [COMPILER_FLAGS...]` checks header
consistency and calls generated adapters from a separately compiled host. It covers
integer boundaries, real special values, mixed signatures, zero and extra arguments,
recursion, version/type/count/pointer rejection, result aliasing and unchanged output
on failure. Empty inventories expose an empty descriptor. Unit tests ensure omitted
recursive definitions do not receive adapters. CI runs GCC/Clang, all three Windows
GCC targets, Linux UBSan and GCC x87 evaluation.

This step does not load libraries, select specializations inside the VM, or change
`CALL`. Those are later stages of the native backend roadmap.
