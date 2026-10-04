# 4. Implementation Structure in C

[Contents](index.md) · [Русский](../ru/04-implementation-in-c.md) · [Previous chapter](03-program-pipeline.md) · [Next chapter](05-lexical-analysis.md)

Edition 2. Implementation described: [commit 8d1fe86, including `println`](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0).

<a id="section-4-1"></a>

## 4.1. How to Read the Implementation

A single Goat operation involves several files. The scanner recognizes `+`, the parser connects operands, a tree node describes addition, and the runtime adds concrete values. Looking for the entire operation in one place makes the project seem confusing. It helps to identify the stage of interest first.

The implementation is written in C. There are no C++ classes, but there are structures, shared interfaces, and tables of function pointers. There is no separate parser generator either: parsing rules are handwritten. This chapter explains the conventions needed to read this code before we turn to individual algorithms.

The main entry point is [main.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/main.c); the preparation and execution sequence lives in [launcher.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/cli/launcher.c). The [CMake build](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/CMakeLists.txt) combines shared sources into the `core` library, used by the interpreter, unit tests, and analysis tests. The functional test runner launches a separate Goat executable.

<a id="section-4-2"></a>

## 4.2. A Directory Map

| Directory | What to look for |
|---|---|
| `src/lib` | Allocation, strings, containers, paths, and input/output |
| `src/common` | Source coordinates, compilation errors, shared types, and control-flow kinds |
| `src/scanner` | Token recognition and token-list operations |
| `src/parser` | Bracket grouping and tree-construction rules |
| `src/graph` | AST nodes, their connections, and source and graph representations |
| `src/analysis` | Abstract values, states, function analysis, and tree transformations |
| `src/codegen` | Code and data builders, C generation, and native module preparation |
| `src/model` | Runtime values, contexts, functions, and loaded libraries |
| `src/vm` | Bytecode, the execution loop, binary format, and garbage collection |
| `src/builtins` | Built-in functions with their runtime and abstract implementations |
| `src/cli`, `src/resources` | Launch options, stage coordination, and message texts |
| `src/test`, `test` | C unit tests and programs with expected results |

This separates responsibilities rather than imposing a strict call sequence between directories. In particular, an operation's bytecode-generation method lives alongside its node in `graph`, while shared instruction builders live in `codegen`. A node may similarly contain an abstract-evaluation method that uses operations from `analysis`.

<a id="section-4-3"></a>

## 4.3. A Shared Structure Prefix and a Method Table

Consider a [binary operation](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/graph/binary_operation.h), an expression with two operands. Its structure begins with the shared expression part:

```c
struct binary_operation_t {
    expression_t base;
    expression_t *left_operand;
    expression_t *right_operand;
};
```

`expression_t` itself begins with `node_t`. This nesting lets code access the shared part of different nodes in the same way. It is a data-layout convention explicitly expressed through C structures. It does not turn an arbitrary pointer into a node of the required kind: a specific method must receive a structure matching its type.

The shared part contains `vtbl`, a pointer to a method table (a set of functions describing the behavior of that node kind). For example, the generic child-count operation in [node.h](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/graph/node.h) uses:

```c
return node->vtbl->get_child_count(node);
```

A binary operation has two children, a literal has none, and a statement list has a program-dependent number. A tree traversal does not need each concrete layout: it asks for the child count and retrieves children by index.

The table also has methods for source rendering, abstract evaluation, bytecode and C generation, and child replacement. Inapplicable actions use [shared methods](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/graph/common_methods.c), such as `no_children`, `no_child_replacement`, and `execute_nothing`. Their results have specific meanings. `no_child_replacement` reports refusal through `false`; `execute_nothing` returns the abstract state unchanged. These are not hidden implementations of missing capabilities.

<a id="section-4-4"></a>

## 4.4. Three Representations of One Number

Seeing the numbers in `2 + 3`, it is tempting to assume that the system stores the same object everywhere. Three levels must actually be distinguished:

| Level | What the number means |
|---|---|
| AST | A literal node: `2` is written at this program location |
| Analysis | An abstract value: exactly `2`, a range, or only a type is known here |
| Execution | An integer object manipulated by VM instructions |

The node belongs to `graph`, the abstract description to `analysis`, and the runtime object to `model`. They serve different purposes and have different lifetimes. The tree may already have been freed when the VM creates the addition's result.

This distinction is visible in [addition.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/graph/addition.c). Its `calculate` method obtains operand descriptions and calls `lattice_add`. Its `generate_bytecode` method emits the left operand's code, then the right operand's code, then `ADD`. Concrete object addition happens later. Thus the name `execute` in the node table should not automatically be read as executing the Goat program: there it refers to abstract interpretation.

Runtime objects also have method tables, but this is a different interface, defined in [object.h](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/model/object.h). Its operations handle concrete values, references, properties, and calls.

<a id="section-4-5"></a>

## 4.5. Memory Is Released by Stage

Many compiler objects are conveniently created individually and released together. An arena (a memory region supplying small allocations without freeing each allocation separately) supports this pattern.

[parser_memory_t](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/lib/arena.h) contains four arenas:

| Field | Contents and period of use |
|---|---|
| `positions` | Source coordinates and ranges, needed beyond parsing |
| `tokens` | The scanner, its working text copy, and tokens; freed after AST construction |
| `graph` | Nodes, associated structures, and analysis data; needed until code preparation finishes |
| `errors` | Compilation error and warning descriptions |

The [arena implementation](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/lib/arena.c) maintains a chain of chunks. A small allocation advances a pointer within the current chunk; a new chunk is added when space runs out. Large allocations receive separate chunks. Previously issued addresses do not move. Alignment (placement at addresses suitable for the corresponding C types) is provided through `memory_alignment_t`.

`destroy_arena` frees the entire chain. An individual node inside an arena must not be passed to `FREE`. Temporary arrays, some containers, and string builders may instead be allocated separately and require their own cleanup. A structure being part of the compiler does not necessarily make it arena-owned.

The [ALLOC, CALLOC, and FREE wrappers](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/lib/allocate.h) centralize allocation and memory accounting. With `MEMORY_DEBUG`, allocation sites are also retained and guard bytes checked. Allocation failure or size overflow terminates the process; it is not a Goat language exception. Runtime objects follow another lifecycle, using reference counts and garbage collection, covered in Chapter 11.

<a id="section-4-6"></a>

## 4.6. Strings and Containers: Who Owns the Data?

Ownership (the responsibility to release memory after use) is not determined merely by the presence of a pointer. [value.h](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/lib/value.h) makes this distinction explicit. `string_view_t` contains an address and length but does not own the string. `string_value_t` additionally contains `should_free`; results are released through `FREE_STRING`, which respects this flag. Length is measured in `wchar_t` units, excluding the terminating zero.

Copying such a structure does not necessarily copy its text. A view into arena storage is usable only while that arena remains alive. For this reason, the [variable constructor](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/graph/variable.c), for example, copies a token's name into the graph arena: the name must survive token cleanup.

[string_builder_t](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/lib/string_ext.h) accumulates text in a growing buffer. Append results share that buffer; a subsequent append may change its address. Once the final string is obtained, the builder is no longer used, and the string is freed once. Intermediate results must not each be freed separately.

Instead of a single universal container, small structures serve specific tasks: a [vector](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/lib/vector.h) for indexed sequences, a [queue](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/lib/queue.h) for deferred function processing, a [linked list](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/lib/linked_list.h) for node sequences, and an [AVL tree](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/lib/avl_tree.h) for key lookup. An AVL tree is an ordered tree kept balanced to bound search length. Freeing a container and freeing its elements are distinct actions; the selected API specifies the required variant.

<a id="section-4-7"></a>

## 4.7. Checking the Explanation

A small investigation can follow a single operation: its constructor, method table, interaction with analysis, and instruction generation. Then move to the runtime object to find where the concrete result is computed. This route does not require reading the entire project at once.

Alignment and arena-growth checks live in [test_lib_safety.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/test/test_lib_safety.c), string checks in [test_string_ext.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/test/test_string_ext.c), and node property and method checks in [test_node_properties.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/test/test_node_properties.c). They verify individual conventions but do not replace reading ownership rules when adding a field or container.

Finally, the base build and additional tools are different layers. Rendering an AST image invokes Graphviz; producing a native library invokes an external C compiler. Neither is needed for token recognition or tree construction themselves. The following chapters examine these stages separately from the later execution paths.
