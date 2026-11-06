# Reading the implementation

[Русский](../ru/index.md)

These guides explain the current implementation to a C programmer who need not
already know compiler theory. Start with the system boundaries, then follow the
proofs, ownership rules and tests relevant to your change.

1. [Implementation map and lifetimes](architecture.md): entry points, directory
   responsibilities and which owner keeps each kind of data alive.
2. [Characters, tokens and bound nodes](front-end.md): reduction order, scopes and
   the difference between a structural walk and execution.
3. [Abstract values and state](abstract-values.md): domains, joins, widening and
   conservative behavior when a fact is unknown.
4. [Function proofs](functions.md): signatures, recursion, effects and C support.
5. [Branches and unreachable code](static-analysis.md): a detailed worked route
   from a condition to a proof and an effect-preserving rewrite.
6. [Execution](execution.md): immutable bytecode, objects, exceptions, threads and GC.
7. [Native generation](native.md): eligibility, emission, ABI and loaded-code lifetime.
8. [Modules](modules.md): construction, layered validation, snapshots and initialization.
9. [Portable support](platform.md): strings, memory, files, processes and cleanup.
10. [Tools](tools.md): diagnostics, inspect/merge and reproducible generators.
11. [Testing](testing.md): independent oracles, scenarios, local checks and CI.
12. [Maintenance](maintenance.md): where explanations belong and how to update them.

## Reading and generating the pages

On GitHub, follow the source/expectation links beside each example. Doxygen embeds
those same files with line numbers: examples are not copied into the prose.
From the repository root, with Doxygen and Bash installed, run:

```sh
bash scripts/check_docs.sh
```

Open `build/doxygen-check/html/index.html` and select the related pages. This
command checks documentation generation and that each embedded analysis example
is registered in the analysis suite; it does not execute that suite. The
[testing guide](testing.md) gives the build and test commands.

Comments beside the implementation explain contracts and non-obvious decisions.
The guides explain how those pieces fit together; they do not replace the code
or promise analysis capabilities that have not been implemented.
