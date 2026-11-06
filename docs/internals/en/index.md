# Reading the implementation

[Русский](../ru/index.md)

These guides are for a C programmer who is new to Goat's implementation and
need not already know compiler theory. Start with the purpose of a pass, then
follow the links to its data structures, decisions and tests.

This first documentation sample covers one complete route:

1. [Conditional branches and unreachable code](static-analysis.md): how a proof
   is obtained, where it is stored and why it does not always permit deletion.
2. [Testing the analysis decisions](testing.md): how source fixtures and explicit
   expectations check those proofs, and how to investigate a failed check.

The wider [implementation book](https://github.com/kniazkov/g0at/blob/master/docs/book/en/index.md) describes the VM, modules
and other subsystems. Further internal guides will follow after this sample is
reviewed; this is not yet a complete map of every subsystem.

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
