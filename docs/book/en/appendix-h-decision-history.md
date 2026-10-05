# Appendix H. Decision History

[Contents](index.md) · [Русский](../ru/appendix-h-decision-history.md) · [Previous](appendix-g-implementation-boundaries.md)

Revision 3. Implementation described: [commit cf51b8c, including `for`](https://github.com/kniazkov/g0at/tree/cf51b8cb27a102d15260c7822ce503404462b0fd).

This is a change index, not a reconstruction of author motives from branch names. Links lead to specific PR discussions and diffs. Current rules come from code and book chapters: historical text may describe an intermediate stage. Earlier entries are established by main-branch merge commits; PR #104 records the loop implementation described in this revision.

| PR | Recorded change and place in the book |
| --- | --- |
| [#19](https://github.com/kniazkov/g0at/pull/19), [#20](https://github.com/kniazkov/g0at/pull/20) | Utility-memory safety and string growth/UTF-8; chapters 4, 11. |
| [#23](https://github.com/kniazkov/g0at/pull/23), [#24](https://github.com/kniazkov/g0at/pull/24) | Observation collector and analysis text format; chapters 15, 27. |
| [#26](https://github.com/kniazkov/g0at/pull/26), [#27](https://github.com/kniazkov/g0at/pull/27) | Lattice laws and independent abstract states; chapters 14, 15. |
| [#28](https://github.com/kniazkov/g0at/pull/28), [#29](https://github.com/kniazkov/g0at/pull/29) | If analysis and unreachability markers; chapters 16, 21. |
| [#30](https://github.com/kniazkov/g0at/pull/30), [#35](https://github.com/kniazkov/g0at/pull/35) | Addition semantics and exception-tagged operation results; chapters 9, 12. |
| [#31](https://github.com/kniazkov/g0at/pull/31), [#32](https://github.com/kniazkov/g0at/pull/32), [#33](https://github.com/kniazkov/g0at/pull/33), [#34](https://github.com/kniazkov/g0at/pull/34) | Exception object, instructions, and try/catch parsing; chapter 12. |
| [#36](https://github.com/kniazkov/g0at/pull/36), [#37](https://github.com/kniazkov/g0at/pull/37), [#42](https://github.com/kniazkov/g0at/pull/42), [#43](https://github.com/kniazkov/g0at/pull/43), [#44](https://github.com/kniazkov/g0at/pull/44), [#45](https://github.com/kniazkov/g0at/pull/45), [#46](https://github.com/kniazkov/g0at/pull/46), [#47](https://github.com/kniazkov/g0at/pull/47), [#48](https://github.com/kniazkov/g0at/pull/48) | Subtraction, unary, arithmetic, comparison, logical, and bitwise operations; appendix B. |
| [#38](https://github.com/kniazkov/g0at/pull/38), [#39](https://github.com/kniazkov/g0at/pull/39), [#40](https://github.com/kniazkov/g0at/pull/40), [#41](https://github.com/kniazkov/g0at/pull/41), [#54](https://github.com/kniazkov/g0at/pull/54) | CI scripts, style, Doxygen, Linux Release, and C-only building; chapter 28. |
| [#49](https://github.com/kniazkov/g0at/pull/49), [#50](https://github.com/kniazkov/g0at/pull/50), [#52](https://github.com/kniazkov/g0at/pull/52) | Call analysis, built-ins, math, int, and input; chapters 13, 17. |
| [#55](https://github.com/kniazkov/g0at/pull/55), [#56](https://github.com/kniazkov/g0at/pull/56), [#57](https://github.com/kniazkov/g0at/pull/57) | Function summaries, signatures, and result types; chapter 17. |
| [#58](https://github.com/kniazkov/g0at/pull/58), [#59](https://github.com/kniazkov/g0at/pull/59), [#60](https://github.com/kniazkov/g0at/pull/60) | Call graph, direct and mutual recursion; chapter 18. |
| [#61](https://github.com/kniazkov/g0at/pull/61), [#62](https://github.com/kniazkov/g0at/pull/62) | Local effects and purity proof; chapter 19. |
| [#63](https://github.com/kniazkov/g0at/pull/63), [#64](https://github.com/kniazkov/g0at/pull/64), [#65](https://github.com/kniazkov/g0at/pull/65), [#66](https://github.com/kniazkov/g0at/pull/66) | C contract, expressions, complete bodies, and specialization display; chapters 20, 27. |
| [#67](https://github.com/kniazkov/g0at/pull/67), [#68](https://github.com/kniazkov/g0at/pull/68) | AST replacements and abs folding; chapter 21. |
| [#70](https://github.com/kniazkov/g0at/pull/70), [#71](https://github.com/kniazkov/g0at/pull/71), [#72](https://github.com/kniazkov/g0at/pull/72), [#73](https://github.com/kniazkov/g0at/pull/73), [#74](https://github.com/kniazkov/g0at/pull/74) | Native contract, generation context, original subtrees, module inventory, and minimal functions; chapters 20–22. |
| [#75](https://github.com/kniazkov/g0at/pull/75), [#76](https://github.com/kniazkov/g0at/pull/76), [#77](https://github.com/kniazkov/g0at/pull/77), [#78](https://github.com/kniazkov/g0at/pull/78), [#79](https://github.com/kniazkov/g0at/pull/79), [#80](https://github.com/kniazkov/g0at/pull/80), [#81](https://github.com/kniazkov/g0at/pull/81) | Numeric C, comparisons/branches, locals, static calls, recursion, modules, and integer helpers; chapter 22. |
| [#82](https://github.com/kniazkov/g0at/pull/82) | Numeric ABI and adapters; chapter 23. |
| [#83](https://github.com/kniazkov/g0at/pull/83), [#84](https://github.com/kniazkov/g0at/pull/84), [#85](https://github.com/kniazkov/g0at/pull/85) | Linux/Windows library builds and loader; chapter 24. |
| [#86](https://github.com/kniazkov/g0at/pull/86), [#87](https://github.com/kniazkov/g0at/pull/87), [#88](https://github.com/kniazkov/g0at/pull/88) | CALL dispatch, native pipeline with retry, and execution modes; chapter 25. |
| [#89](https://github.com/kniazkov/g0at/pull/89) | Checking AST replacements against specialization proofs; chapters 21, 22. |
| [#90](https://github.com/kniazkov/g0at/pull/90) | Separate compilation and execution; chapter 26. |
| [#91](https://github.com/kniazkov/g0at/pull/91) | Call-tree performance test; chapter 29. |
| [#92](https://github.com/kniazkov/g0at/pull/92) | Corrupt-binary tests through the shared in-memory decoder; chapter 26. |
| [#93](https://github.com/kniazkov/g0at/pull/93) | Static MinGW runtime linking in generated DLLs; chapter 24. |
| [#97](https://github.com/kniazkov/g0at/pull/97) | println with runtime and abstract checks; chapter 13 and appendix D. |
| [#104](https://github.com/kniazkov/g0at/pull/104) | Scoped C-style for loops, widened abstract execution, native loops and numeric updates; chapters 2, 6–8, 16, 20, 22, 25, 28. |

<a id="section-h-1"></a>

## H.1. History of the book itself

[PR #94](https://github.com/kniazkov/g0at/pull/94) recorded the plan; [#95](https://github.com/kniazkov/g0at/pull/95) added chapter 1; [#96](https://github.com/kniazkov/g0at/pull/96) added part I. These were followed by [part II, #98](https://github.com/kniazkov/g0at/pull/98), [part III, #99](https://github.com/kniazkov/g0at/pull/99), [part IV, #100](https://github.com/kniazkov/g0at/pull/100), and [part V, #101](https://github.com/kniazkov/g0at/pull/101).

[Part VI and appendices, #102](https://github.com/kniazkov/g0at/pull/102), completed the book. [#103](https://github.com/kniazkov/g0at/pull/103) removed the temporary plan and superseded standalone notes. Adding println produced revision 2; support for for loops produces revision 3, pinned to commit cf51b8c. Adding chapters alone does not extend interpreter capabilities.

For a stable technical fact, prefer pinned source or a book section at a specific commit. A PR is useful when showing the transition itself: what code appeared, which checks were added, and what changed relative to the parent state.
