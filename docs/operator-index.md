# Index getter and setter handlers

[Русская версия](operator-index.ru.md)

For user objects, `a[i,j,k]` selects the `"[]"` property through ordinary
property/prototype lookup after evaluating the receiver and all indices once,
left to right. A function receives the original receiver as `this` and each
index as a separate argument. `a[]` passes no arguments. Its result may be any
value. Changes to the handler during index evaluation affect the current read;
reassigning the variable `a` does not replace the saved receiver.

`a[i,j,k] = value` evaluates the receiver, indices and RHS once in that order,
then selects `"[]="` immediately before writing. Arguments are the indices
followed by value. `a[] = value` passes only value. The setter's normal return
is discarded: the assignment expression returns the original value by identity,
even if the setter changes a parameter or returns another object. A thrown
exception interrupts the assignment and retains earlier effects.

```goat
const storage = [0];
const box = {};
box["[]"] = func() { return storage[0]; };
box["[]="] = func(value) { storage[0] = value; return 999; };
println(box[] = 7); // 7, not 999
println(box[]);     // 7
```

Only functions qualify, including closures and builtins. A present non-function
raises `INVALID_OPERATION`; an inaccessible property raises its access error.
Neither is treated as absence. Failed-file lookup retains the original exception
value and trace. An inherited handler still receives the original object, with
ordinary method access rights and the actual arguments array.

Only an absent handler selects the built-in indexed operation. Its arity and
access rules are unchanged: ordinary property indexing needs one index, and
arrays need one in-range integer. Named `a.name` reads and writes do not call
index handlers. Handler lookup itself uses ordinary properties, not recursive
index calls. No new user-visible bypass of index handlers is provided; a handler
can capture a separate storage object, as in the example.

## Evaluation, preflight and continuations

`CHECKINDEX` leaves user-object destinations alone: a RHS can install a setter
that did not previously exist. Consequently, user-object fallback arity, access,
constant-property and foreign-object write errors occur after the RHS at the
write stage. The earlier RHS effects remain. For compound indexing, an error
reading the old value still stops before the RHS. Named variable/property checks
and the early specialized array write checks are unchanged.

The VM reuses `GETINDEX`/`SETINDEX` and their index count; the parser's existing
empty/multiple-index grammar and source reconstruction are unchanged. The common
operator dispatcher resumes normal execution after a Goat handler or a synchronous
builtin. `SETINDEX` keeps its original RHS on the operand stack as a GC root and
uses the discard-result continuation. Nested calls, finally blocks and exception
unwinding use the existing frame/stack ownership rules.

The call ABI accepts up to 65535 actual arguments. A found getter with more than
65535 indices, or setter with more than 65534 indices plus value, raises
`INVALID_ARGUMENT` rather than truncating the count. This is checked at execution
for directly linked and freshly decoded images. No instruction, format version,
old-bytecode path or executable-code rewriting was added.

Analysis treats every index read, including a constant-key read on a module/file
scope, as potentially effectful with an unknown result. Mutable facts cannot
survive it; named module-member analysis remains separate. Native handler
integration is tracked in #503, not claimed by this VM step.

The C/CTest `operator_index_testing` suite covers source regeneration, plain and
optimized execution, fresh decoding, immutable images, handler selection, actual
arguments, GC, unwinding, real module scopes and exact failed-file provenance.
