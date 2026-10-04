# Native library loading

`src/model/native_library.h` exposes one internal interface. `native_library.c`
validates and owns metadata; `native_library_linux.c` and `native_library_windows.c`
provide the OS operations. The public provider contract remains
[`include/goat/native_abi.h`](../include/goat/native_abi.h).

`load_native_library(path)` accepts an explicit absolute or relative filesystem path.
Linux resolves it with `realpath` and uses `dlopen(RTLD_NOW | RTLD_LOCAL)`.
Windows resolves it with `GetFullPathNameA` and uses `LoadLibraryExA` with
`LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`; dependencies
may come from the library directory and the standard safe search directories.
Windows filenames currently follow the project's narrow-character path convention.
Missing dependencies and incompatible binary architectures are OS loading failures.

## Validation

The loader resolves `goat_native_query_v1` and requests ABI version 1. It checks:

- ABI version, module/entry/value sizes, value alignment and pointer width;
- aligned tables, matching counts and pointers, numeric parameter and result tags;
- non-null adapters and known flags;
- unique specialization IDs and unique parameter signatures within each function;
- consistent optional binding names within a function, with no name shared by two functions;
- nonempty, well-formed UTF-8 binding names, when present.

An empty module has zero entries and a NULL table. An entry with zero parameters
has a NULL parameter table. Return type alone cannot distinguish specializations.
IDs are module-local, so separate libraries may reuse them.

Limits are 65,536 entries, 65,535 formal parameters per entry, 4,096 bytes per name,
and 64 MiB of copied signatures and names. Metadata is copied before publication;
validation failure releases all partial allocations and the OS library reference.

This loads **trusted native code**: constructors and the query execute before
validation. Providers must supply valid readable pointers and keep metadata immutable
during loading. Structural checks do not sandbox code or make arbitrary addresses safe.

## Ownership

A successful result owns one library reference. An unsuccessful result owns a diagnostic
and reports `OPEN_FAILED`, `QUERY_MISSING`, `ABI_MISMATCH`, `INVALID_METADATA`, or
`UNSUPPORTED`. Destroy the result in either case. Destruction clears its owned pointers
and can be repeated on the same result; do not shallow-copy an owning result.

A function descriptor groups all specializations for one module-local `function_id`
and retains its library. It can outlive the load result:

```c
native_library_result_t loaded = load_native_library(path);
if (loaded.status == NATIVE_LIBRARY_OK) {
    native_function_descriptor_t *function =
        create_native_function_descriptor(loaded.library, function_id);
    destroy_native_library_result(&loaded);
    if (function) {
        const goat_native_entry_v1_t *entry = get_native_function_entry(function, 0);
        /* entry and its adapter remain valid while function is retained. */
        (void)entry;
        release_native_function_descriptor(function);
    }
} else {
    /* Consume loaded.diagnostic before destroying the result. */
    destroy_native_library_result(&loaded);
}
```

Missing function IDs return NULL. Indexed access outside the table returns NULL.
Metadata getters return borrowed, read-only entries. Retain the owner for as long as
an entry or adapter is in use, including the entire duration of an adapter call.
Descriptors and libraries support explicit retain/release; references must already
be live when retained. Atomic reference counts do not make other runtime facilities
thread-safe.

Metadata has no dependency on the analysis arena or AST lifetime. The final descriptor
release drops its library reference. The final library release frees its snapshot and
calls `dlclose` or `FreeLibrary`; actual OS unloading also depends on other OS references.

## Tests and integration boundary

`scripts/check_native_loader.sh <goat> <output-directory>` compiles a generated module,
handwritten providers, malformed metadata and a provider with a missing dependency.
Tests exercise failure cleanup, UTF-8, size limits, empty modules, metadata snapshots,
shared descriptor ownership and actual unload notifications. A generated Fibonacci
adapter is called after destroying the original load result. The script runs in both
Linux compiler jobs and all three Windows jobs.

Descriptors can now be attached to `FUNC` metadata and retained by Goat function
objects for dispatch through ordinary `CALL`; see the [execution contract](native-execution.md).
Automatic compile/load integration and bounded-recursion retry follow separately.
