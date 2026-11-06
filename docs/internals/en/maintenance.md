# Maintaining comments and guides

[Contents](index.md) · [Русский](../ru/maintenance.md)

## What belongs beside a declaration

State why a function exists and which stage calls it. Add non-obvious ownership,
lifetime, failure and partial-result rules. Explain units, sentinels or ordering
when the type/name does not express them. Do not repeat every self-explanatory
parameter, getter or assignment. English is used for C comments; overview pages
have matching English and Russian versions.

Put an algorithm's reasoning next to the decision that depends on it: why a join
ignores terminated paths, why a loader delays publication, or why an unknown call
invalidates knowledge. Keep exact API rules in the header and connect them to a
guide with `@see`; do not maintain multiple copies of a long contract.

## When a subsystem changes

1. Review its entry point, owner/destructor and error paths, not just the edited line.
2. Update the appropriate page from the [implementation map](architecture.md) in
   both languages. Distinguish a current limitation from an invariant.
3. Prefer a registered fixture when explaining an analysis rule. Include the actual
   `.goat` and `.expect` files; do not paste a second independently edited program.
4. Run the relevant suite and documentation generation. Check the generated HTML,
   because a link working on GitHub can still be unresolved in Doxygen.
5. Keep CI status separate from prose review. Passing tests establish their stated
   assertions, not complete correctness of the documentation.

## Reading the generated documentation

Run `bash scripts/check_docs.sh` from the repository root and open
`build/doxygen-check/html/index.html`. The check configuration includes production
code, public headers, tools and test code/support. Generated Unicode/schema tables
are excluded from API extraction: their source and update workflow are documented
in [tools](tools.md). Third-party code is outside this guide's ownership scope.

The Markdown filter expands `<!--! ... -->` commands for Doxygen 1.9.8. GitHub
hides those directives and exposes adjacent canonical source links. Each embedded
analysis source must have a nonempty expectation file and be in the normal test
manifest. This validation does not execute the tests; use [testing](testing.md).

The older implementation book explicitly describes named revisions. Use it for
background, but verify present-day paths and contracts against these guides and
current headers before changing code. These overviews cover subsystem boundaries;
they are not a claim that every helper needs a new comment.
