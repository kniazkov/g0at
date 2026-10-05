# 7. AST, Scopes, and Name Binding

[Contents](index.md) · [Русский](../ru/07-ast-and-name-binding.md) · [Previous chapter](06-syntax-analysis.md) · [Next chapter](08-bytecode-generation.md)

Edition 3. Implementation described: [commit cf51b8c, including `for`](https://github.com/kniazkov/g0at/tree/cf51b8cb27a102d15260c7822ce503404462b0fd).

<a id="section-7-1"></a>

## 7.1. The Tree Exists, but Names Are Not Yet Bound

The parser can construct a variable node for `x` without knowing where that variable was declared. Recognizing a name is sufficient for syntax. It is insufficient for analysis: identical names may refer to different variables, while separate uses sometimes need to lead to one declaration.

After parsing, the AST (abstract syntax tree) is therefore augmented with scopes, parent pointers, and declaration links. This stage takes the root node and produces a structure suitable for determining which data the program reads and modifies. The main pass is in [analysis.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/analysis/analysis.c) and runs even with `--optimize none`.

<a id="section-7-2"></a>

## 7.2. Nodes and Connection Kinds

The common [node_t](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/graph/node.h) stores a method table, parent, source range, scope, traversal identifier, and analysis flags. A concrete node adds its own data: a literal adds a value, a binary operation adds operands, and a declaration adds a list of introduced names.

It helps to distinguish the categories in [node_type.h](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/graph/node_type.h):

| Category | Examples and purpose |
|---|---|
| Structural nodes | Root, parameter list, function body |
| Declarators | One node for each variable, constant, or parameter name |
| Expressions | Literal, variable, call, operation, function, block expression |
| Statements | Declaration, return, `throw`, expression wrapper |
| Control flow | Condition and exception handler |
| Replacements | Original and transformed versions of an expression or statement |

A declarator (a node introducing one name) is not the whole declaration statement. In `var a = 1, b = 2;`, one declaration contains two declarators. Analysis must be able to distinguish their values.

The tree has structural edges, such as those from addition to its operands. It also has semantic links, such as a variable use referring to its declarator. The latter does not make the declaration a child expression of the variable. Separate `get_related` methods and the `RELATION_DECLARATION` connection kind serve this purpose in [variable.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/graph/variable.c). The representation thus becomes a graph of connections, while the construct tree remains the basis of traversal.

<a id="section-7-3"></a>

## 7.3. Assigning Scopes and Identifiers

A scope is represented by [scope_t](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/graph/scope.h), with a parent and a name table. [Lookup](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/graph/scope.c) checks the current scope first, then visits its parent. The nearest declaration found shadows an outer declaration with the same name.

The built-in environment's scope is created first. A traversal then sets node `parent` and `scope` fields, creates nested scopes for functions and ordinary blocks, and queues functions for binding. `try/catch` additionally separates the protected-code scope from the handler scope; the exception name belongs to the handler.

`node_t.id` is assigned during traversal. Numbering restarts at function boundaries but continues through ordinary blocks. It is neither a globally unique node number across all functions nor a permanent address in a saved program. Synthetic nodes added after the initial traversal may retain a zero identifier.

The traversal also clears previous analysis results: flags, pointwise expression values, function summaries, and other computed information. Repeated analysis must rebuild them rather than treat an old result as proof for a new tree state.

`for` introduces a scope shared by its initializer, condition, step, and body. Its body has a nested scope even without explicit braces. `var i` in the header therefore shadows an outer `i`. By contrast, `for (i = 0; ...)` assigns an existing binding; an otherwise unknown `i` follows the ordinary implicit-declaration rule. That synthetic declaration is inserted and registered in the enclosing statement list, so later uses resolve to the same binding.

<a id="section-7-4"></a>

## 7.4. One Spelling, Different Variables

[Scope and capture example](../examples/07-bindings.goat):

```goat
var x = 10;
{
    var x = 20;
    println(x);
}
println(x);
const make = func() {
    const read = func() { return value; };
    var value = 30;
    return read;
};
const read = make();
println(read());
try { throw 40; } catch (x) { println(x); }
println(x);
```

Output:

```text
20
10
30
40
10
```

The example exposes several independent bindings:

| Name use | What it refers to |
|---|---|
| `x` printed inside the ordinary block | Local `var x = 20` |
| `x` after the ordinary block | Outer `var x = 10` |
| `value` in the nested `read` body | `var value = 30` inside `make` |
| `x` in the handler | The exception value introduced by `catch (x)` |
| `x` after the handler | Outer `var x = 10` again |

On encountering a declarator, the binding pass adds it to the current scope's table. On encountering a variable node, it looks up the declaration and stores a pointer in `variable_t.declarator`. Subsequent analysis uses that connection to distinguish the states of identically named variables.

Built-in names participate in lookup too. They use a shared synthetic declarator (a service node without an ordinary source declaration); the specific built-in function is additionally identified by its name and registry entry. Thus that one pointer alone does not distinguish `print` from `println`. A local declaration with the same name receives its own declarator and shadows the built-in value normally.

<a id="section-7-5"></a>

## 7.5. Why Nested Functions Are Bound Later

Inside `make`, `read` is written before the declaration of `value`. An ordinary sequential traversal immediately entering each function would not yet have encountered the required declaration. Goat uses a queue: it first binds the enclosing scope while skipping nested function bodies, then processes the deferred functions. This order ensures an enclosing function is handled before a nested one.

By the time `read` is bound, `value` is already in the enclosing scope's table. The name use receives a link to it and becomes a capture (a function's reference to a variable from an enclosing environment). The same order resolves references between functions declared later in an enclosing scope.

Binding a name and executing a declaration are different actions. A link to a later declarator does not mean its initializer has already run. In our example, `read` is called after `make` finishes, so `value` has already received `30`. This order is intentional; binding alone does not prove the correctness of every earlier call.

The AST records the declaration link. The [function object](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/model/function.c) retains the concrete environment on function creation and extends the lifetime of captured values. The example's function can therefore continue reading `value` after leaving `make`.

<a id="section-7-6"></a>

## 7.6. An Unknown Name Creates a Declaration

If lookup finds no name, the current implementation creates an implicit variable declaration without an initializer. It is inserted before the statement containing the use, in a suitable statement list. [Example](../examples/07-implicit.goat):

```goat
println(missing);
missing = 5;
println(missing);
```

It prints `null`, then `5`, each on a new line. To see the inserted declaration on Linux:

```sh
./goat --optimize none --enable-warnings --print-source-code docs/book/examples/07-implicit.goat
```

In Windows PowerShell:

```powershell
.\goat.exe --optimize none --enable-warnings --print-source-code .\docs\book\examples\07-implicit.goat
```

The reconstructed source contains `var missing;` before the first call. A warning identifies the use of `missing` before declaration; the program then executes. The warning and program output go to different streams, so their relative on-screen ordering may depend on the environment.

Insertion itself is deferred until binding traversal finishes: adding a child immediately would change the indices and sequence currently being visited. The new declarator is nevertheless registered in the scope at once so subsequent uses can find it. After insertion, the new subtree receives parent links and the existing scope.

This behavior is convenient for short experiments, but a typo can become a new variable. Explicit `var` and `const`, together with warnings, help reveal this. An implicit declaration is an implemented rule, not evidence that the analyzer has proven the author's intent.

<a id="section-7-7"></a>

## 7.7. The Original Tree and Analysis Results

An expression node can hold `immediate_value`: information about its value at a specific point of immediate execution. A declarator holds `abstract_value`: an accumulated description of values assigned to it. Function specializations are stored separately. These data cannot automatically substitute for one another: a variable name, a particular use, and a function for a complete type signature answer different questions.

Node flags mark established properties: unreachability, purity, and compatibility with the supported C subset. A missing flag does not prove the opposite property. In particular, absence of proven purity does not mean an effect necessarily occurs on every run.

During simplification, a [replacement node](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/graph/replacement.c) retains both the original and new variants. Bytecode execution uses the replacement result; history remains available for inspection and restoration. [restore_graph](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/analysis/simplification.c) restores original subtrees before a new analysis.

Because history is retained, some descendants can be shared between old and new variants. `parent` therefore denotes the parent in the executable tree; it cannot simultaneously be interpreted as a unique owner along every historical edge. The arena retains the node memory itself. The storage model matters here; the conditions under which replacement preserves behavior will be examined in Chapter 21.

<a id="section-7-8"></a>

## 7.8. An Analysis Scope Is Not a VM Context

A static scope records name-to-declaration bindings during program preparation. A [VM context](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/model/context.h) holds concrete execution data and information for returning or handling an exception. Two calls to one function create different execution contexts even though the function tree and static scope are the same.

The `variable_t.declarator` link is not written into bytecode as a value-cell address. [Variable code generation](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/graph/variable.c) uses a name in the data segment and the `VLOAD`/`STORE` instructions; the runtime searches its environment objects. The analyzer instead distinguishes declarations by their nodes. These mechanisms serve different system stages.

Useful checks include the [analysis shadowing example](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/test/analysis/shadowing.goat), the [local function named println](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/test/analysis/purity_shadow_println.goat), [changes to an enclosing environment](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/test/functional/external_scope_changing/program.goat), and [tree-restoration tests](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/test/test_replacement.c). This chapter's example additionally shows a later declaration captured by a closure and the locality of a handler name.

After binding, the system has both the notation's structure and the links needed to reason about the program. Next, that structure will become bytecode, and static names will receive concrete values in the runtime.
