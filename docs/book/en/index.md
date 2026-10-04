# Goat Internals: Interpreter, Static Analyzer, and Native Backend

Red callouts mark limitations and known defects of the implementation described.

## Contents

### Part I. Purpose and Overall Structure

1. [Project Purpose](01-project-purpose.md)
2. [The Implemented Language](02-implemented-language.md)
3. [A Program Through the System](03-program-pipeline.md)

### Part II. Source Text and Program Representation

4. [Implementation Structure in C](04-implementation-in-c.md)
5. [Lexical Analysis](05-lexical-analysis.md)
6. [Syntax Analysis](06-syntax-analysis.md)
7. [AST, Scopes, and Name Binding](07-ast-and-name-binding.md)

### Part III. Bytecode and Runtime

8. [Bytecode Generation](08-bytecode-generation.md)
9. [Virtual Machine and Value Model](09-vm-and-values.md)
10. [Contexts, Functions, and Closures](10-contexts-and-closures.md)
11. [Memory Management](11-memory-management.md)
12. [Exceptions and Runtime Errors](12-exceptions.md)
13. [Built-in Functions](13-builtins.md)

### Part IV. Static Analysis

14. [Abstract Values and the Lattice](14-abstract-values.md)
15. [Abstract State](15-abstract-state.md)
16. [Expression and Control-Flow Analysis](16-expression-and-control-analysis.md)
17. [Call Analysis and Specializations](17-call-analysis-and-specializations.md)
18. [Call Graph and Recursion](18-call-graph-and-recursion.md)
19. [Effects and Purity](19-effects-and-purity.md)
20. [Native Compilation Eligibility](20-native-eligibility.md)
21. [AST Transformations and Semantic Preservation](21-ast-transformations.md)

### Part V. Native Backend

22. [C Code Generation](22-c-code-generation.md)
23. [Native ABI](23-native-abi.md)
24. [Library Compilation and Loading](24-library-compilation-and-loading.md)
25. [Native Dispatch and VM Fallback](25-native-dispatch-and-vm-fallback.md)
26. [Separate Compilation](26-separate-compilation.md)

### Part VI. Testing and Investigation of the Implementation

27. Observability
28. Testing
29. Performance Measurement

### Appendices

- A. Terminology
- B. Language
- C. VM Instructions
- D. Built-in Functions
- E. Launch Interfaces
- F. Implementation Map
- G. Implementation Boundaries
- H. Decision History
