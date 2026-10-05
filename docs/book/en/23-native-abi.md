# 23. Native ABI

[Contents](index.md) · [Русский](../ru/23-native-abi.md) · [Previous chapter](22-c-code-generation.md) · [Next chapter](24-library-compilation-and-loading.md)

Revision 3. Implementation described: [commit cf51b8c, including `for`](https://github.com/kniazkov/g0at/tree/cf51b8cb27a102d15260c7822ce503404462b0fd).

<a id="section-23-1"></a>

## 23.1. A contract between separately built parts

The VM is compiled ahead of time; a library is built later from a particular program. They must agree on number representations, memory layouts, and function calls. That contract is an ABI: an application binary interface. Matching function names alone is insufficient.

The contract is defined in [native_abi.h](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/include/goat/native_abi.h). Its current version is 1. Windows uses `__cdecl` (the convention for passing arguments and managing the stack), and the exported entry point uses `__declspec(dllexport)`. C++ declarations have `extern "C"` linkage, without C++ name mangling.

VM objects, AST nodes, and closure contexts do not cross this interface. The boundary is much narrower: numeric arguments, a numeric result, specialization metadata, and a completion status.

<a id="section-23-2"></a>

## 23.2. A value with a type tag

`goat_native_value_v1_t` contains two `uint32_t` fields, `type` and `reserved`, followed by a union of `int64_t integer` and `double real`. A union (alternative fields sharing storage) carries no type of its own; `type` identifies the active representation.

| Tag | Number | Contents |
|---|---:|---|
| `GOAT_NATIVE_INVALID` | 0 | Not a valid numeric argument |
| `GOAT_NATIVE_I64` | 1 | The `integer` field |
| `GOAT_NATIVE_F64` | 2 | The `real` field |

`reserved` must be zero. It belongs to the checked format, rather than representing another program value. Structure size and alignment (the required address boundary in memory) are checked against library metadata; readers need not assume identical structure packing across arbitrary compilers.

Integer `3` and real `3.0` have different tags. The adapter performs no implicit conversion between them. The exact signature established by analysis is preserved at the call boundary.

<a id="section-23-3"></a>

## 23.3. An adapter around an ordinary C function

An internal function has ordinary typed parameters and a typed result. For external calls, the generator adds an adapter (a wrapper translating a uniform format into a specific signature). All adapters share one pointer type:

```c
uint32_t (GOAT_NATIVE_CALL *invoke)(
    uint32_t abi_version,
    uint32_t argument_count,
    const goat_native_value_v1_t *arguments,
    goat_native_value_v1_t *result);
```

[c_adapter.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/codegen/c_adapter.c) first checks the version, pointers, sufficient argument count, tags, and zero reserved fields of arguments in use. There may be more arguments than formal parameters. Missing arguments or an unsuitable type prevent the body from being called.

The adapter then extracts numbers, calls the internal C function, and writes a result with the required tag. It prepares the result in a local structure and copies it to `*result` only on success. A generated adapter's failure preserves the previous result contents.

Internal calls, such as `calculate` → `twice` in chapter 22, need no packing: both C functions already know their types. The adapter is needed at the VM/library boundary, not for every arithmetic operation.

<a id="section-23-4"></a>

## 23.4. The table and entry point

One exported function, `goat_native_query_v1`, accepts a version number and returns a pointer to `goat_native_module_v1_t`. The generated module returns `NULL` for an unsupported version.

The module description contains the version, its own structure size, value size and alignment, specialization-entry size, pointer size, entry count, and table pointer. A `goat_native_entry_v1_t` entry contains:

| Fields | Purpose |
|---|---|
| `specialization_id`, `function_id` | Distinguish the specialization and source function |
| `parameter_count`, `parameter_types` | Describe formal arguments |
| `return_type` | Describe the result |
| `invoke` | Call the adapter |
| `binding_name` | Optional binding name in the general ABI |
| `flags` | Properties, including `GOAT_NATIVE_PURE` |

Generated modules set `binding_name` to `NULL` and mark entries as pure. Program binding uses identifiers, rather than searching DLL exports for a source name such as `twice`.

<a id="section-23-5"></a>

## 23.5. What the receiving side checks

[native_library.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/model/native_library.c) checks the version and sizes, table alignment, valid numeric tags, adapter pointer, flags, and duplicate identifiers and signatures. Entry count is capped at 65,536, parameter count at `UINT16_MAX`, and the copying budget for type tables and names at 64 MiB. An optional name must be nonempty UTF-8 of at most 4096 bytes.

Metadata is copied into loader-owned structures. Code pointers remain tied to the loaded library. Beyond general ABI validation, the native pipeline compares entries with the expected module: identifiers, signatures, purity, and corresponding `FUNC` instructions.

> [!CAUTION]
> ABI version 1 transfers only integer and real values. Size validation does not convert between architectures or arbitrary compiler conventions. Pointer and metadata checks also do not isolate faulty machine code: the library is loaded into the interpreter process.

<a id="section-23-6"></a>

## 23.6. A status instead of a Goat exception

| Status | Number | Meaning |
|---|---:|---|
| `GOAT_NATIVE_OK` | 0 | A result was produced |
| `GOAT_NATIVE_TYPE_MISMATCH` | 1 | Arguments do not match the signature |
| `GOAT_NATIVE_BAD_REQUEST` | 2 | Invalid request or result |
| `GOAT_NATIVE_ABI_MISMATCH` | 3 | Incompatible interface version |
| `GOAT_NATIVE_RESOURCE_LIMIT` | 4 | A defined resource limit was reached |
| `GOAT_NATIVE_EXTERNAL_ERROR` | 5 | External failure |

This is the ABI status space, not a list of language exceptions or a list of every status emitted by the generated wrapper. In particular, having `EXTERNAL_ERROR` does not imply that the adapter catches arbitrary hardware faults.

On `OK`, the VM also checks the result tag and reserved field. An invalid result is not converted into a Goat object. Chapter 25 describes the permitted retry after `RESOURCE_LIMIT`; not every unsuccessful call may be retried automatically.

<a id="section-23-7"></a>

## 23.7. A testable boundary

For chapter 22's module, querying version 1 yields a table of four specializations: two functions, each with integer and real variants. Another version yields `NULL`. Calling the integer adapter for `calculate` with argument `3` produces `OK` and integer `7`; a real tag is rejected before the body runs.

The generator embeds ABI definitions in standalone C text, so compiling a saved module does not require the Goat header tree. This creates an obligation to keep two forms of the same contract synchronized. [check_native_abi.sh](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/scripts/check_native_abi.sh) checks the generated interface with a consumer using the public header, including failures and preservation of the output value. Loading and metadata checks are in [check_native_loader.sh](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/scripts/check_native_loader.sh).
