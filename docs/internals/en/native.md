# Native generation and runtime dispatch

[Contents](index.md) · [Русский](../ru/native.md)

## Proof, emission and execution are separate gates

The analyzer's C-support fact only says the inspected specialization fits its
supported contract. `src/codegen/c_generation.h` records a separate generation
status and the first failing node. Whole-function generation returns complete
source or explicit failure, never a partly usable function. The lowering context
tracks declaration identities, local representations, exact callee specializations,
temporaries and helpers. Statement preludes preserve operand evaluation order.

Scalar helpers preserve Goat behavior where ordinary C operators would introduce
undefined behavior or a different result. String adapters in `src/native` and the
public `include/goat` headers define the boundary rather than exposing arbitrary
runtime object layouts to generated code.

prepare_native_execution() joins compilation, loading, validation and binding while
analysis storage is still alive. A failed preparation must not leave a fresh
bytecode object partly bound. Native preparation results distinguish empty output,
compiler, load, binding and cleanup failures. A compiler process exiting zero does
not prove that its library exports compatible metadata.

## Retain code as long as any function can call it

`native_library_t` owns the OS library and validated metadata. Function descriptors
retain the library; runtime functions and bytecode bindings can retain descriptors.
The borrowed entry address is usable only while that chain stays alive. Releasing
the AST is safe after generation; releasing the library while a retained function
exists is not. Artifact digests and ABI checks are separate from this lifetime rule.

For modules, prepare_module_snapshot_native() checks selection across an already
validated graph and installs lazy resolvers. Preparation itself neither extracts
payloads nor loads libraries. Required mode needs an eligible root-owned
specialization, not merely one in a dependency. Off mode does not select platforms.
On demand, the loader uses the retained snapshot and shared cleanup service.

## Built-ins connect runtime and analysis

`builtin_function_t` supplies an ordinary executor, an abstract interpreter and
effect metadata; methods can additionally use a receiver-aware executor. These
parts must agree. For example, describing an input operation as pure can make an
optimization incorrect even when its runtime executor is perfect. Output alone
and changes to bindings have different invalidation consequences.

`src/builtins/registry.c` exposes built-ins; `src/model` wraps their stable object
identities; `src/analysis/native_builtin.c` consumes their analysis behavior.
Review runtime argument rules, abstract domains, effects and native eligibility
together when adding a built-in. Existing wrappers and descriptors should be reused.

## Verification route

Run the analysis suite for eligibility and rejection facts, unit tests for lowering
and descriptor lifetime, and native integration scenarios for real compiler/load/
invoke paths. `ctest -N -L native` lists the configured native cases. Performance
scenarios are separate and serialized: timing compilation as part of execution
would answer a different question. Native reports and retained scenario commands
help distinguish “not proven”, unavailable toolchain, fallback and a backend fault.

Detailed contracts: [native scalars](https://github.com/kniazkov/g0at/blob/master/docs/native-scalars.md),
[module native API](https://github.com/kniazkov/g0at/blob/master/docs/module-native-api.md),
[dispatch](https://github.com/kniazkov/g0at/blob/master/docs/module-dispatch-api.md).
