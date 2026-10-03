# Native backend roadmap

Each step is intended as a separate reviewable PR. The
[native execution contract](native-execution.md) governs the implementation.

1. Define numeric specialization, execution and fallback contracts.
2. Introduce specialization-aware node emission interfaces and explicit failures.
3. Lower replacement nodes through original subtrees as a conservative baseline.
4. Inventory module specializations, deterministic C names and exact dependencies.
5. Emit a minimal complete numeric function; compile and run standalone tests.
6. Lower unary signs and addition/subtraction/multiplication with Goat semantics.
7. Lower comparisons, conditions and `if/else`.
8. Lower fixed-representation locals, assignments, scopes and shadowing.
9. Lower static function calls, preserving argument evaluation order.
10. Support direct and mutual recursion in standalone generated C.
11. Add deterministic complete-module output with `--print-c` and `--save-c`.
12. Define a versioned numeric adapter ABI with explicit execution status.
13. Compile dynamic libraries on Linux, with diagnostics and temporary-file cleanup.
14. Add Windows compilation/export support and numerical compatibility checks.
15. Load and validate libraries; establish descriptor and library ownership.
16. Attach descriptors through function-creation metadata and dispatch in the function
    object's call method before allocating a context. Keep `CALL`; introduce no `NATIVE` opcode.
17. Connect the pipeline and enforce safe native recursion limits with bytecode retry.
18. Add native execution modes and cross-mode CI that verifies actual native entry.
19. Revisit replacement handling: audit the shared-AST equivalence contract, prove
    simplified subtrees per specialization, prefer proven replacements for C, and
    keep specialization-only rewrites separate from the shared AST.

Until step 19, the original-subtree policy from step 3 remains in effect. It is a
conservative proof-reuse policy, not a claim that current shared-AST replacements
are unsafe for ordinary bytecode execution.
