# StringBuilder

`StringBuilder` is a built-in prototype in the root context, beside `String` and
`Integer`; no import is required.

```goat
var b = StringBuilder -> {};
b += "test";
b << ' ' << 123;
b.append("!");
const text = b.toString(); // "test 123!"; str(b) is equivalent
println(b.length);        // 9 Unicode code points
b.clear().append("next"); // text is still "test 123!"
```

- `append(value)` takes exactly one argument. It first converts it using the same
  conversion and access context as `str(value)`, then appends the resulting text.
  It propagates the original exception and trace unchanged; failed conversion
  leaves the builder untouched. Later changes to the input do not affect the text.
- `<<`, `+=`, and `append` mutate and return the same receiver. Aliases see the
  mutation. Goat implements `+=` using `+` followed by assignment, so **`b + value`
  also appends in place**. This follows the existing compound-assignment contract;
  there is no separate `+=` handler. A constant binding cannot be a `+=` target;
  `append` and `<<` can still mutate the referenced instance.
- `toString()` and `str(b)` return independent string snapshots, including embedded
  zero characters. Self append works by the same conversion-before-append rule.
- `length` is a public, read-only count of Unicode code points, not bytes or grapheme
  clusters. Empty builders have length zero. Named and indexed writes raise
  `Exceptions.PROPERTY_IS_CONSTANT`.
- `clear()` takes no arguments, resets length to zero and returns the receiver.
  Allocated capacity is retained. The UTF-32 buffer grows geometrically, using the
  existing C string builder. Allocation failure follows the runtime allocator policy.
- `StringBuilder.init` participates in the ordinary ancestor constructor chain.
  It creates empty **own** storage on the receiver before derived initializers and
  the right-hand block run. A diamond invokes the base initializer once. Deriving
  from a populated builder starts empty; properties and methods still inherit
  normally. Repeated manual `init()` is idempotent and preserves existing contents.
- The built-in prototype itself has no mutable buffer. `append`, `clear`, `init`,
  `+`/`+=`, and `<<` on that prototype raise `Exceptions.IMMUTABLE_OBJECT`.
- `clone()` and `flat()` copy the receiver's native buffer independently, without
  invoking constructors. Ordinary user fields retain their usual shallow-copy rules.
  Buffer lifetime follows the object, including reference-count release, cycle
  collection, process shutdown and object-pool reuse.

A detached method called without a receiver raises `Exceptions.INVALID_OPERATION`.
Wrong method arity raises `Exceptions.INVALID_ARGUMENT`. No general dispatch to a
user-defined `toString()` is introduced: StringBuilder is handled by the runtime's
string conversion, also used by printing and container rendering.

Tests cover source and saved GBIN with optimization none/all, VM and native
execution (native requires optimization all), unchanged saved images, inherited
construction, diamonds, alias identity, Unicode/zero characters, growth, snapshots,
self append, arity, read-only length, immutable prototypes, clone/flat and native
storage cleanup. Failed-module fixtures compare `str` and append's exact exception
value and trace for object and null payloads.
