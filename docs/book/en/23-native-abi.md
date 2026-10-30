# 23. Native ABI

[Contents](index.md) · [Русский](../ru/23-native-abi.md) · [Previous chapter](22-c-code-generation.md) · [Next chapter](24-library-compilation-and-loading.md)

Updated in #220 against the implemented baseline [5b98271](https://github.com/kniazkov/g0at/tree/5b98271e26cb17bdc5589aaf827316016ba2e00a). Commit-pinned links to earlier revisions below are historical implementation references; current contracts are linked explicitly.

<a id="section-23-1"></a>

## 23.1. Two distinct interfaces

An ABI is the contract between the already-built VM and separately compiled native code. The implementation has two interfaces, not two interchangeable layouts:

- Standalone source/program compilation uses [native_abi.h](../../../include/goat/native_abi.h), `goat_native_query_v2` and `goat_native_*_v2_t`. Its version argument is `GOAT_NATIVE_ABI_VERSION`; the suffix alone is not that number.
- ZIP modules use [module_abi.h](../../../include/goat/module_abi.h), the `goat.adapter.v1` contract, embedded descriptor `goat_module_descriptor_v1`, and adapters `goat_fn_<specializationId>_v1`.

Both use C linkage and `__cdecl` on Windows/x86. Never cast structures or function pointers from one interface to the other. Neither exposes VM objects, AST nodes or closure contexts to native code.

<a id="section-23-2"></a>

## 23.2. Values and strings

The module ABI has exact tags for null, integer, real, string, boolean and character. A `goat_module_value_v1` occupies 16 bytes with alignment 8, including on MINGW32: tag, zero reserved field, and a 64-bit payload. The borrowed string view also occupies 16 bytes with alignment 8 and carries a pointer plus a **uint64** scalar count. Strings contain 32-bit Unicode scalars and may contain zero; they are not NUL-terminated C strings.

Representation, padding, boolean values, Unicode scalars, readable ranges and string lengths are checked. A result string is copied before the call context and arguments are released. Integers are not converted to real values merely to match a specialization. A scalar type supported by the ABI does not imply that every operation on it is supported by the C generator.

<a id="section-23-3"></a>

## 23.3. One checked call

The module adapter accepts the ABI version, host context, argument count, an array of values and a separate result buffer. Before entry, the VM applies access checks, ordinary call semantics and the specialization’s declarative guards. Missing arguments count as null; extra arguments have already been evaluated once. Guards do not call Goat code or overloaded operators.

The host initializes the result and validates both status and returned representation. No matching specialization means VM execution **before** any native attempt. An adapter already entered is never replayed on the VM under the module ABI, even for `RESOURCE_LIMIT`. See [dispatch](../../module-dispatch-api.md). The standalone query ABI has a separate, narrowly checked pure-resource fallback described in chapter 25.

<a id="section-23-4"></a>

## 23.4. Descriptor and identity

A bounded `GOATND01` descriptor contains canonical JSON with a uint64 little-endian length. Packaging checks the real ELF/PE descriptor and adapter exports without loading the library. Runtime loading validates readable mapped ranges before copying and comparing the embedded descriptor to the selected archive entry.

A specialization belongs to a function in one concrete module instance. Numeric `functionId` values are not global names. Archive metadata, bytecode function tables, guards and signatures must agree. Overlapping native alternatives for one call are an error, not “first match wins.” The [ABI](../../module-abi-api.md) and [consistency](../../module-consistency-api.md) references contain exact validation rules.

<a id="section-23-5"></a>

## 23.5. Host services and ownership

The context carries a host service table and opaque per-call state. Allocation, deallocation and Unicode services use the host contract; native code must not replace them with a private allocator or free borrowed inputs. Only the outer owner performs bulk cleanup after consuming the result. Unicode services use the pinned Unicode version, currently 17.0.0.

Code pointers retain their loaded library. Shared module state, native libraries and scopes survive until all program threads, including descendants, have finished. Lazy extraction and loading use the common cleanup service; they do not mutate the validated bytecode image.

<a id="section-23-6"></a>

## 23.6. Errors and exit

A malformed result, false status, or other detected ABI violation after module adapter entry is terminal: no VM replay, Goat catch, or later thread join may resume user code. This is distinct from an ordinary language exception explicitly saved by a host service.

Both service tables have an optional `request_exit` tail, checked by size and callback presence before use. Valid `exit(0..255)` goes through the shared whole-process mechanism and never returns. An invalid argument or forbidden initialization context saves a Goat exception and returns `HOST_EXCEPTION`; the adapter must immediately propagate it. A fabricated host-exception status, or continuing after that failure, is an ABI violation. The original mandatory service-table prefix remains compatible. See [exit](../../module-exit-api.md).

Loading or invoking native code is **not a security sandbox**. Initializers and arbitrary faulty machine code cannot be made safe merely by checking metadata.

<a id="section-23-7"></a>

## 23.7. Tests and contract sources

[Module ABI tests](../../../test/src/functional/module_abi_scenarios.c), [native value tests](../../../scripts/check_module_native_values.py), and [dispatch tests](../../../test/src/functional/module_dispatch_consistency_scenarios.c) check real libraries, representations and one-shot failure behavior. Standalone ABI compatibility remains covered by `check_native_abi.sh` and `check_native_loader.sh`.

The public headers, [v1 format](../../module-format-v1/README.md), and linked API references are the exact contracts. Chapter 22’s numeric program illustrates one supported subset; it does not limit the ABI to numeric values.
