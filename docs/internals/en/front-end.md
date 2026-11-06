# From characters to bound nodes

[Contents](index.md) · [Русский](../ru/front-end.md)

## Scanner: preserve positions while changing representation

create_scanner() copies the length-delimited UTF-32 input into token-arena storage.
Comment characters and carriage returns are replaced with spaces; newlines are
kept so later diagnostics still point to the original rows. Quotes and escapes
prevent comment markers inside literals from being removed. An embedded zero is
not end-of-source: the scanner uses length and a sentinel outside Unicode.
The filename is borrowed for source positions; the caller must preserve it.

get_token() identifies tokens and puts them into role-based groups as well as
source order. This second organization lets the parser visit, for example,
operators of one precedence without repeatedly scanning every token.

## Parser: reduction order expresses grammar

process_brackets() establishes nested token lists and diagnoses bracket mismatch.
apply_reduction_rules() then runs the ordered rules in `src/parser/parser.c`.
Multiplication precedes addition; assignment and exponentiation use backward
traversal. Changing traversal direction changes associativity, so a new operator
needs tests for chained operands as well as a single use.

collapse_tokens_to_token() replaces an inclusive range in the linked list with an
AST-bearing token. Removed tokens stay arena-owned; unlinking is not freeing.
The rules also collect function objects for deferred bytecode generation.
process_root_token_list() completes the root and validates loop transfers; a
nested function does not inherit its enclosing loop's break/continue target.

## AST: several protocols on the same structure

`node_t` and `node_vtbl_t` in `src/graph/node.h` define structural children,
non-structural relations, abstract calculation/execution, rewriting, bytecode/C
emission and visualization. A relation to a declaration is not a child to execute.
Do not use a generic graph walk as a substitute for language evaluation order.

An expression's calculate callback produces an abstract value and may affect
state. A statement's execute callback advances abstract control flow. Neither
executes the user's program in the VM. Function bodies are deferred code, so
visiting a function literal must not imply that its body runs at that point.

## Binding: declaration identity matters

analyze() restores archived originals before a repeat analysis, assigns parents,
scopes and IDs, and binds enclosing functions before nested ones. This permits
closures to resolve declarations in their enclosing scope. Synthetic declarations
are inserted after traversal so insertion cannot invalidate child indexes.

Two equal spellings in different scopes denote different declarators. Access
checks, captures, state maps and C local bindings must preserve this identity.
Dynamic inheritance lookup can remain unresolved even when a lexical candidate
exists. Optimization cannot turn that candidate into an unconditional lookup.

With optimization disabled, binding, iteration validation and access analysis
still run; abstract interpretation and rewriting do not. Rejection by the parser,
semantic diagnostics and loss of optimization precision are separate outcomes.

## Verify a syntax change

Use `core.unit` for scanner/parser/node contracts, `core.analysis` for binding and
analysis facts, and `core.functional` for observable language behavior. For
source includes also inspect `src/parser/source_includes.h` and the source-graph
scenarios described in [modules](modules.md). A parsing-only assertion cannot
prove that the resulting operation evaluates its operands in the right order.
