# Cross-module operator persistence fixtures

`module_operator_scenarios` builds three independent modules and compares direct
`.gmod` execution with fresh processes consuming only their `.gbin` archives.
Both optimized and unoptimized bytecode are checked in VM, native-auto and
native-required modes. Persisted consumers have no source files, a missing
selected compiler and an isolated PATH. Exact native counters prove entry.

The receiver and its operator function have different declaring modules. The
function retains its included file, private lexical values, closure and exception
trace. Returned `arguments` retains original object identity after parameter
reassignment and allocation-driven GC. An initializer's exception is cached and
its preceding output is not repeated when the failed module is imported again.

Operator properties installed with ordinary indexed assignment retain default
file visibility. Exporting the object does not widen it. The semantics fixture
uses an exported wrapper in the receiver's file and verifies that direct foreign
access still raises `ACCESS_DENIED`. The C component fixture separately verifies
explicitly external operator slots and owner/file identities.
