# Implementation Plan for Access Modifiers and Copying

> Historical stages A–G below are implementation history. Stage H is complete; the current rules are in [appendix I](appendix-i-access-control.md).

[Contents](index.md) · [Русский](../ru/access-control-implementation-plan.md) · [Rules](appendix-i-access-control.md) · [Examples](access-control-examples.md)

Stages A–H are implemented. The imperative descriptions below record the original work plan; old format numbers and #154 references are historical, superseded by portable v1 and the [module checklist](../../module-implementation-checklist.md). Current restrictions are stated in appendix I and the implementation-boundaries appendix.

## 1. Changes Required in the Existing Model

At the PR #160 baseline, `src/model/user_defined_object.c` stores properties in a key-indexed AVL tree; `property_value_t` holds a value and a constness flag. This cannot represent multiple private fields with the same key. `clone` copies own pairs and retains prototypes; function values remain shared references. Special handling of direct object self-references must survive the refactor.

`src/builtins/flat.c` visits the source and its topology, keeps the first property found for each key, and excludes `Object`. Shadowed values are currently discarded. The new model must retain all transferred cells while separately selecting ordinary lookup winners.

`src/model/function.c` stores a closure environment as `object_t *closure`. Method invocation creates a context incorporating the closure and receiver; `this` is associated with the current Goat context. Do not replace this with Java-like `this` or use the caller as the source of a method's rights. Captures need explicit links to cells, and executing functions need module/access-domain identity.

Property operations, key enumeration, and string conversion in `src/model/object.h` currently accept no access context. Checking only `GETPROP` would leave bypasses through indexing, built-ins, printing, and copying. A shared checking layer and a separate internal path for full-state transfer are required.

## 2. Stage A: Cells, Provenance, and Lifetime

**Status: the internal foundation is implemented in #162.** The API is in `src/model/access.h`. Modules and domains are retained identities, not global current-execution variables. Each new user object (including reuse of pooled memory) gets a fresh domain. Contexts store modules separately; a call context uses the function's module. Built-in entities have a separate immortal module identity and public property visibility.

An object owns the full inventory of stable cells; the AVL tree only references ordinary-lookup winners. A capture stores a storage-object/cell pair and retains storage, allowing multiple functions to share one cell. GC traverses the full inventory, captures, and all active contexts; identities are reference-counted without a back-reference to the object. Tests cover hidden fields, arbitrary keys, shared captures, cycles, and pool reuse.

At this stage, `clone` preserves all cells and their original metadata, but does not rebind domains or function captures yet: that is stage D. Access checks, automatic capture binding, new syntax, and metadata serialization are not implemented; the bytecode format and native ABI are unchanged. The stage requirements below are retained for reference.

Introduce internal entities without changing syntax yet:

- Module identity: store the new object's owner, field declaring module, and function module separately. Before #154, the program receives one ordinary module; built-ins receive explicit public visibility.
- Declaration-domain identity for a concrete object, not just an AST or name. Two activations of one factory must create distinct private domains.
- A field descriptor/cell: arbitrary object key, value, constness, modifier, declaring module, and domain owner. Separate the full cell inventory from the ordinary key-lookup index.
- Function access rights and capture references to cells. Preserve shared captures and explicit references to old objects.

Identity tables and cell mappings are a proposed representation; choose concrete C structures after reviewing ownership. Do not use one mutable global “current module” or “current object”: calls, threads, and reentrancy must not mix rights.

Change points: `src/model/user_defined_object.c`, `object.h`, `function.c`, `context.[ch]`, `thread.[ch]`, and `src/vm/gc.c`. GC must see hidden/shadowed cells, adapted function environments, and retained original references; reference counting and cycle destruction must also remain correct.

Acceptance: unit tests for distinct owners sharing a key, shared captures, independent factory objects, cycles, and memory release; existing single-module results remain unchanged. New modifiers are not advertised as supported yet.

## 3. Stage B: Unified Access and Ownership Checks

**Status: the internal layer is implemented in #163.** `access_check.c` centralizes property resolution, visibility checks, writes, and creation. The VM uses it for names, dot access, ordinary object indices, and method selection; compound assignments and variable updates use the same instructions. Arrays and other specialized index implementations retain virtual dispatch. `++`/`--` still accept variables only: this stage does not extend their syntax.

Each context owns a rights snapshot: its module, private domains, and separate protected prototype domains. Lexical scopes inherit it; a call uses the function's snapshot; return/exception unwinding destroys it with the context. Only explicit inheritance grants protected prototype rights. An external function's receiver and lookup topology never grant those rights, including inside nested blocks or closures. Built-ins receive the caller's existing `thread`, carrying this snapshot, without switching to the built-in module.

Access denial produces `Exceptions.ACCESS_DENIED`; extending a foreign object produces `Exceptions.FOREIGN_OBJECT`; constness remains a separate `PROPERTY_IS_CONSTANT`. Absence is distinct within the model and retains the language's previous null read. Lookup never skips an inaccessible winner. Private slots use a separate resolution path.

Language-level `clone`/`flat` create an object in the caller's module, preserve the declaring module and modifier of transferred fields, and allocate fresh domains without granting original methods access to the copy. Transfer does not use user-level reads. Function/capture adaptation and full transfer of shadowed `flat` cells remain stages D/E; observation filtering remains stage F. Keywords and multi-module source programs therefore remain disabled. Model and VM tests inject metadata directly: two modules, dynamic indices, detached methods, factories, protected descendant access, copies, nested closures, returns, and exceptions.

Define an immutable view of the executing code's access: function module, access domains, and receiver/prototype relationships. Calls switch to the callee's context; returns and exceptions restore the caller's context. Built-in observation and copying operations receive their caller's context, not arbitrary privileges of the built-in object.

Check reads, writes, property creation, and method selection under shared rules. Distinguish missing properties, access denial, constant writes, and forbidden extension of foreign objects. Do not continue lookup after denial. A method's private slot and ordinary protected/public key lookup must remain distinct resolution paths.

Cover name access, `GETPROP`/`SETPROP`, indexing, method calls, compound assignments, and updates. Continue evaluating the left-hand side and indices once. Checks must not change array indexing rules. Internal cell transfer for copying is not a user read and does not use the observation filter.

Acceptance: model/VM tests for AC-03–AC-10, AC-18, AC-24–AC-28, including two synthetic modules without the #154 loader. Test dynamic receivers, variable indices, detached function calls, exception returns, and nested calls without privilege leakage.

## 4. Stage C: Declarations, Binding, and Bytecode

Implemented in #164: syntax and VM bindings, binary format 11, source/binary round trips, and invalid metadata rejection. Later stages D–G complete copying and observation; optimization and native execution now follow the proofs and limits described in G.

Add three scanner tokens, parse a single modifier and implicit `const`, and apply the modifier to the full declaration list. Preserve metadata in declaration nodes, name bindings, and property-creation instructions. Support every scope, source regeneration, and AST visualization.

Change points: `src/scanner`, `src/parser/parsing_declarations.c` and related variable rules, `src/graph/declarations.[ch]`, name binding, `src/vm/opcodes.h`, `bytecode.c`, `vm.c`, and EN/RU messages. Bind a lexical capture to its original cell: a captured prototype field must not turn into dynamic lookup of a descendant field (AC-13).

Serialize identities and metadata in `src/vm/binary.c` with a new format version. Validate table bounds, references, modifier values, and corrupt input. Do not serialize C addresses or temporary process object identities as stable file identifiers. Choose the next available format version during implementation rather than reserving one here.

Acceptance: AC-01–AC-10 execute or are rejected as specified; source and bytecode round trips pass at both optimization levels. Incompatible old formats are explicitly rejected. Keywords can now be enabled in the integration branch; complete functionality requires the following stages.

## 5. Stage D: Shared Transfer Machinery and clone

**Implemented in #165.** `model/transfer.[ch]` keeps operation-local storage, domain, cell, and function maps. Clone allocates own cells first, allocates each directly held dynamic function once, then links environments and values. Function code remains shared; native descriptors are discarded. Built-in function values and nested objects stay shared. New rights derive only from existing grants; original grants remain for explicit original references. Field and function modules survive a change of object owner.

Regression tests cover AC-14–AC-20 clone behavior, repeated copies, detached calls, private writes through names and this, shared external activation variables, recursive/mutually recursive methods, and unprivileged attached functions. Model tests exercise foreign-module ownership/default visibility, capture identity, constness, and repeated GC with only a copied detached method rooted. Full flat transfer is implemented in stage E (#166); observation and static analysis are implemented in stages F–G.

Create a per-operation copying context mapping old domains, cells, and functions to new ones. Allocate new domains/cells first, then link values, functions, and environments. Two-phase transfer is needed for mutual references, recursive functions, and state shared by multiple methods.

For `clone`, transfer own cells and retain the same prototype objects. The calling module owns the new object; field/function modules are preserved. Adapt directly transferred functions once per function identity. Redirect captures of transferred cells and retain external captures. Transfer only existing rights; retain authorized explicit references to originals; do not mutate original functions.

Do not recursively traverse nested objects and arrays. Inherited fields and methods remain in shared prototypes under `clone`. Preserve existing constness, self-reference, and virtual `clone` rules for other types; revising those rules was not part of the discussion.

Acceptance: AC-04–AC-08, AC-14–AC-20, AC-25, AC-28. Add repeated copying, two copies of one object, detached methods, recursive/mutually recursive captures, and GC after external original references disappear. Check values and function identity, not just successful calls.

## 6. Stage E: Complete flat with Preserved Order

**Implemented in #166.** Flat reuses the stage D transfer maps. It selects real ancestors, filters the existing ordered topology, and copies every own cell once. Only the first ordinary winner is indexed by key; hidden/shadowed cells remain available to methods. Object is excluded and remains the sole parent. As before, transferred flat fields are mutable, while modifiers and declaring modules are preserved. Non-user objects still return themselves.

Contexts distinguish lexical lookup fallbacks from inheritance edges. Adapted method environments preserve external lexical captures without exposing those environments as result parents. Concrete capture records distinguish factory instances with identical bytecode IDs; internal environment views are adapted again when a flattened object is cloned or flattened. Protected receiver grants are transferred from the original inheritance graph, without granting private rights to unrelated functions.

Tests cover AC-11–AC-17 and AC-19–AC-22 behavior, independent hidden cells, two shared ancestors in a diamond, a 32-level chain, 16 factory prototypes, recursive methods, repeated flat/clone, detached calls, arbitrary keys, and external captures. Model tests check a foreign function module (AC-26), lookup winners, hidden-cell counts, flat mutability, and GC with only a detached method rooted. AC-23 observation filtering is implemented in stage F (#167); preserving its hidden data and method behavior is implemented here.

Reuse stage D's verified machinery. Obtain topology preserving the old lookup priority; transfer the source and each ancestor except `Object` exactly once by identity. Retain all fields, including same-named private and shadowed protected fields. Build ordinary lookup winners separately from method-to-cell relationships.

Shared-ancestor deduplication in a diamond and function deduplication solve different problems; both are necessary. Internal domains of transferred ancestors do not enter the result's prototype list. Its sole parent is `Object`. Non-user-defined types retain existing `flat()` behavior; do not silently turn the operation into a deep copy.

Acceptance: AC-11–AC-17, AC-19–AC-23, AC-26. Additional graphs: long chains, wide prototype sets, diamonds with multiple shared ancestors, and distinct objects from one template. Compare lookup before/after, transferred cell independence, shared internal captures, and absence of old parents from `instanceOf`. Preserve existing arbitrary-object-key, flat constness, and non-user-defined receiver tests.

## 7. Stage F: Observation and Built-ins

Implemented in #167. Snapshots contain only accessible own ordinary-lookup keys; hidden inventory cells and prototypes are not enumerated. One explicit observer context is retained through nested objects, arrays, and object keys. An object emptied by filtering prints as `{ }`. The context flows through print/println, str, format, string/character concatenation, Array.toString, and unhandled-exception reporting after stack unwinding. Functions still print as `func`, without their environment. clone/flat do not use this filter. Tests cover own/outside methods, descendants, copies, restored rights after return/throw, cycles, snapshots, and distinct modules. Static analysis and native execution remain stage G.

Apply access context to the `for … in` key snapshot and standard object representation (`str`, `print`, and related formatting paths). Internal owner identifiers and hidden cells must not appear in user output. Preserve existing snapshot semantics; accessible own keys must not become an implicit prototype traversal.

Check object formatting inside arrays and other objects: recursive rendering must retain observer context. Check observation from an own method, an outside function, and after returning from a privileged method. Function/environment rendering must not disclose hidden values.

Acceptance: AC-11, AC-23 and paired external/internal observer, nested-container, and copy tests. Complete copying must retain data even when the same caller cannot enumerate it. Do not implement `flat` using publicly filtered keys.

## 8. Stage G: Analysis, Optimization, and Native Execution

**Implemented in #168.**

Stage G (#168) distinguishes allowed, denied, and unknown access. Direct closed fields of objects bound through immutable lexical names (including constant alias chains) produce compile errors identifying the operation, name, and declaration location. This applies with `--optimize none` and inside `try`. Dynamic receivers or keys, factory/clone/flat results, cross-function constant captures, private-slot precedence, inherited lookup, and call-dependent protected grants retain VM checks. A captured const is not necessarily immutable after flat: its transferred cell may be changed. Unknown access never means a missing property.

For programs containing modifiers, optimization currently folds only closed scalar expressions: no name reads, calls, properties, or scope elimination. Modifiers, declaration identities, and captures remain intact; in particular, a constant source field does not turn a read of a mutable flat cell into a literal. This restricts optimization rather than disabling permission checks.

The native contract includes an `access` blocker. Proven scalar local operations, including modified local declarations, and proven string receivers can execute natively. Accesses needing VM rights remain in the VM; unknown user objects never gain an unchecked native path. With no safe functions, `--native required` reports `Access checks require VM execution`. This mode requires a native implementation in the program, not native execution of every call. Copied functions do not reuse original native descriptors; method calls retain the existing VM path.

Checks include static rejection, dynamic equivalents, English/Russian diagnostics, both optimization levels, regenerated source and bytecode, safe native calls, and required-mode rejection. Compiler-unavailable fallback and VM retry after the native depth limit are tested: side-effecting argument expressions run once, and closed fields remain closed after calls. The module loader and multi-file scenarios remain #154.

Track proven permitted/forbidden/unknown access in analysis and preserve metadata through AST transformations. Definite denial is a compile error identifying the operation and declaration; uncertainty keeps a runtime check. Optimization must not replace an inaccessible property with its constant value or mistake denied access for a missing field.

Until a native path is proven safe, functions requiring access checks remain in the VM. `--native required` reports the reason explicitly. A copied function does not automatically inherit the original native specialization's eligibility. Check that fallback and retry after a native call preserve module/rights and do not repeat side effects.

Acceptance: static negative tests and dynamic exceptions for equivalent scenarios, identical results with optimization off/on, native tests for proven-safe cases and required-mode rejection. Run these checks before releasing the feature as a whole.

## 9. Stage H: completed module integration

#193/#204 introduced real file/module access boundaries; #194/#195 execute include/import, #228 preserves function provenance, and #218 checks AC-01–AC-28 through the real source compiler and archive loader. Export across a module boundary requires `external`, not `public`. Model tests remain complementary, not a substitute for loading.

The [real-file coverage table](access-control-examples.md) links the executable fixtures. They cover factory ownership, copies retaining field provenance, protected descendants, forbidden foreign extension, constness, observation and native-call guards. Existing CI runs these checks at both optimization levels and through compiled archives.

## 10. Order and Readiness Boundaries

Sequence: A → B → C → D → E → F → G; H uses that foundation when implementing #154. Each PR includes tests for its invariants. Review access representation, memory, and serialization in A rather than postponing them to the end. Private domains, cloning, and model-level module rules can be tested before the loader exists.

Three main acceptance conditions: access does not depend on an accidental call path; copying neither grants new privileges nor mutates original environments; `flat()` preserves field selection, captures, and diamond sharing while removing the original prototype chain. Any semantics discovered during implementation that cannot be derived from these rules must be discussed rather than silently chosen.
