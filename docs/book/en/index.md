# Goat Internals: Interpreter, Static Analyzer, and Native Backend

## Contents

### Part I. Purpose and Overall Structure

1. Project Purpose
2. The Implemented Language
3. A Program Through the System

### Part II. Source Text and Program Representation

4. Implementation Structure in C
5. Lexical Analysis
6. Syntax Analysis
7. AST, Scopes, and Name Binding

### Part III. Bytecode and Runtime

8. Bytecode Generation
9. Virtual Machine and Value Model
10. Contexts, Functions, and Closures
11. Memory Management
12. Exceptions and Runtime Errors
13. Built-in Functions

### Part IV. Static Analysis

14. Abstract Values and the Lattice
15. Abstract State
16. Expression and Control-Flow Analysis
17. Call Analysis and Specializations
18. Call Graph and Recursion
19. Effects and Purity
20. Native Compilation Eligibility
21. AST Transformations and Semantic Preservation

### Part V. Native Backend

22. C Code Generation
23. Native ABI
24. Library Compilation and Loading
25. Native Dispatch and VM Fallback
26. Separate Compilation

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
