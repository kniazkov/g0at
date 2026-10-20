# Implementation Plan for Access Modifiers and Copying

[Contents](index.md) · [Русский](../ru/access-control-implementation-plan.md) · [Rules](appendix-i-access-control.md) · [Examples](access-control-examples.md)

This is a proposed technical plan for implementing the agreed semantics, not a claim that the changes exist. AC-01–AC-28 refer to the example catalog. The stages are intended as sequential, reviewable PRs; stages may be combined, but partially enforced protection must not be released.

## 1. Changes Required in the Existing Model

At the PR #160 baseline, `src/model/user_defined_object.c` stores properties in a key-indexed AVL tree; `property_value_t` holds a value and a constness flag. This cannot represent multiple private fields with the same key. `clone` copies own pairs and retains prototypes; function values remain shared references. Special handling of direct object self-references must survive the refactor.

`src/builtins/flat.c` visits the source and its topology, keeps the first property found for each key, and excludes `Object`. Shadowed values are currently discarded. The new model must retain all transferred cells while separately selecting ordinary lookup winners.

`src/model/function.c` stores a closure environment as `object_t *closure`. Method invocation creates a context incorporating the closure and receiver; `this` is associated with the current Goat context. Do not replace this with Java-like `this` or use the caller as the source of a method's rights. Captures need explicit links to cells, and executing functions need module/access-domain identity.

Property operations, key enumeration, and string conversion in `src/model/object.h` currently accept no access context. Checking only `GETPROP` would leave bypasses through indexing, built-ins, printing, and copying. A shared checking layer and a separate internal path for full-state transfer are required.

## 2. Stage A: Cells, Provenance, and Lifetime

Introduce internal entities without changing syntax yet:

- Module identity: store the new object's owner, field declaring module, and function module separately. Before #154, the program receives one ordinary module; built-ins receive explicit public visibility.
- Declaration-domain identity for a concrete object, not just an AST or name. Two activations of one factory must create distinct private domains.
- A field descriptor/cell: arbitrary object key, value, constness, modifier, declaring module, and domain owner. Separate the full cell inventory from the ordinary key-lookup index.
- Function access rights and capture references to cells. Preserve shared captures and explicit references to old objects.

Identity tables and cell mappings are a proposed representation; choose concrete C structures after reviewing ownership. Do not use one mutable global “current module” or “current object”: calls, threads, and reentrancy must not mix rights.

Change points: `src/model/user_defined_object.c`, `object.h`, `function.c`, `context.[ch]`, `thread.[ch]`, and `src/vm/gc.c`. GC must see hidden/shadowed cells, adapted function environments, and retained original references; reference counting and cycle destruction must also remain correct.

Acceptance: unit tests for distinct owners sharing a key, shared captures, independent factory objects, cycles, and memory release; existing single-module results remain unchanged. New modifiers are not advertised as supported yet.

## 3. Stage B: Unified Access and Ownership Checks

Define an immutable view of the executing code's access: function module, access domains, and receiver/prototype relationships. Calls switch to the callee's context; returns and exceptions restore the caller's context. Built-in observation and copying operations receive their caller's context, not arbitrary privileges of the built-in object.

Check reads, writes, property creation, and method selection under shared rules. Distinguish missing properties, access denial, constant writes, and forbidden extension of foreign objects. Do not continue lookup after denial. A method's private slot and ordinary protected/public key lookup must remain distinct resolution paths.

Cover name access, `GETPROP`/`SETPROP`, indexing, method calls, compound assignments, and updates. Continue evaluating the left-hand side and indices once. Checks must not change array indexing rules. Internal cell transfer for copying is not a user read and does not use the observation filter.

Acceptance: model/VM tests for AC-03–AC-10, AC-18, AC-24–AC-28, including two synthetic modules without the #154 loader. Test dynamic receivers, variable indices, detached function calls, exception returns, and nested calls without privilege leakage.

## 4. Stage C: Declarations, Binding, and Bytecode

Add three scanner tokens, parse a single modifier and implicit `const`, and apply the modifier to the full declaration list. Preserve metadata in declaration nodes, name bindings, and property-creation instructions. Support every scope, source regeneration, and AST visualization.

Change points: `src/scanner`, `src/parser/parsing_declarations.c` and related variable rules, `src/graph/declarations.[ch]`, name binding, `src/vm/opcodes.h`, `bytecode.c`, `vm.c`, and EN/RU messages. Bind a lexical capture to its original cell: a captured prototype field must not turn into dynamic lookup of a descendant field (AC-13).

Serialize identities and metadata in `src/vm/binary.c` with a new format version. Validate table bounds, references, modifier values, and corrupt input. Do not serialize C addresses or temporary process object identities as stable file identifiers. Choose the next available format version during implementation rather than reserving one here.

Acceptance: AC-01–AC-10 execute or are rejected as specified; source and bytecode round trips pass at both optimization levels. Incompatible old formats are explicitly rejected. Keywords can now be enabled in the integration branch; complete functionality requires the following stages.

## 5. Stage D: Shared Transfer Machinery and clone

Create a per-operation copying context mapping old domains, cells, and functions to new ones. Allocate new domains/cells first, then link values, functions, and environments. Two-phase transfer is needed for mutual references, recursive functions, and state shared by multiple methods.

For `clone`, transfer own cells and retain the same prototype objects. The calling module owns the new object; field/function modules are preserved. Adapt directly transferred functions once per function identity. Redirect captures of transferred cells and retain external captures. Transfer only existing rights; retain authorized explicit references to originals; do not mutate original functions.

Do not recursively traverse nested objects and arrays. Inherited fields and methods remain in shared prototypes under `clone`. Preserve existing constness, self-reference, and virtual `clone` rules for other types; revising those rules was not part of the discussion.

Acceptance: AC-04–AC-08, AC-14–AC-20, AC-25, AC-28. Add repeated copying, two copies of one object, detached methods, recursive/mutually recursive captures, and GC after external original references disappear. Check values and function identity, not just successful calls.

## 6. Stage E: Complete flat with Preserved Order

Reuse stage D's verified machinery. Obtain topology preserving the old lookup priority; transfer the source and each ancestor except `Object` exactly once by identity. Retain all fields, including same-named private and shadowed protected fields. Build ordinary lookup winners separately from method-to-cell relationships.

Shared-ancestor deduplication in a diamond and function deduplication solve different problems; both are necessary. Internal domains of transferred ancestors do not enter the result's prototype list. Its sole parent is `Object`. Non-user-defined types retain existing `flat()` behavior; do not silently turn the operation into a deep copy.

Acceptance: AC-11–AC-17, AC-19–AC-23, AC-26. Additional graphs: long chains, wide prototype sets, diamonds with multiple shared ancestors, and distinct objects from one template. Compare lookup before/after, transferred cell independence, shared internal captures, and absence of old parents from `instanceOf`. Preserve existing arbitrary-object-key, flat constness, and non-user-defined receiver tests.

## 7. Stage F: Observation and Built-ins

Apply access context to the `for … in` key snapshot and standard object representation (`str`, `print`, and related formatting paths). Internal owner identifiers and hidden cells must not appear in user output. Preserve existing snapshot semantics; accessible own keys must not become an implicit prototype traversal.

Check object formatting inside arrays and other objects: recursive rendering must retain observer context. Check observation from an own method, an outside function, and after returning from a privileged method. Function/environment rendering must not disclose hidden values.

Acceptance: AC-11, AC-23 and paired external/internal observer, nested-container, and copy tests. Complete copying must retain data even when the same caller cannot enumerate it. Do not implement `flat` using publicly filtered keys.

## 8. Stage G: Analysis, Optimization, and Native Execution

Track proven permitted/forbidden/unknown access in analysis and preserve metadata through AST transformations. Definite denial is a compile error identifying the operation and declaration; uncertainty keeps a runtime check. Optimization must not replace an inaccessible property with its constant value or mistake denied access for a missing field.

Until a native path is proven safe, functions requiring access checks remain in the VM. `--native required` reports the reason explicitly. A copied function does not automatically inherit the original native specialization's eligibility. Check that fallback and retry after a native call preserve module/rights and do not repeat side effects.

Acceptance: static negative tests and dynamic exceptions for equivalent scenarios, identical results with optimization off/on, native tests for proven-safe cases and required-mode rejection. Run these checks before releasing the feature as a whole.

## 9. Stage H: Integration with #154 and Completion

Module identity is introduced in A; the real module loader arrives in #154. Then map stable compilation-module identifiers to runtime module instances, export `public` declarations, carry function module identity through calls, and restore metadata when loading bytecode separately. This plan does not specify import syntax.

Before #154, model tests use two identities to check module rules. With the loader, AC-25–AC-28 become full multi-file functional tests. Cover B calling an A factory; a B-owned copy with A-declared fields; a B descendant of A; forbidden extension of the original; and protected access across the boundary.

Final verification: the general functional suite, analyzer, saved source/bytecode, native regression, UBSan/memory checks, and existing Linux/Windows/ARM64 builds. CI complements local checks; run relevant suites at each stage and the full suite before integrating the completed implementation. Add cases to existing test lists rather than creating individual CI jobs.

Update both book languages as implementation proceeds, stating actual status. Once complete, move rules from design status into the language reference; do not advertise multi-file execution before #154.

## 10. Order and Readiness Boundaries

Sequence: A → B → C → D → E → F → G; H uses that foundation when implementing #154. Each PR includes tests for its invariants. Review access representation, memory, and serialization in A rather than postponing them to the end. Private domains, cloning, and model-level module rules can be tested before the loader exists.

Three main acceptance conditions: access does not depend on an accidental call path; copying neither grants new privileges nor mutates original environments; `flat()` preserves field selection, captures, and diamond sharing while removing the original prototype chain. Any semantics discovered during implementation that cannot be derived from these rules must be discussed rather than silently chosen.
