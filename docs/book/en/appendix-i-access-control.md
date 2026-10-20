# Appendix I. Access Modifiers: Agreed Rules

[Contents](index.md) · [Русский](../ru/appendix-i-access-control.md) · [Previous](appendix-h-decision-history.md)

Status: language design, the agreed portion of the discussion on October 20, 2026, ahead of issue #154. This section does not claim that access modifiers are implemented. Examples specify expected behavior of the future implementation. Copying methods and their access rights still needs discussion; open questions appear at the end.

## I.1. Declarations

The keywords `public`, `protected`, and `private` appear only before a variable or constant declaration. A declaration may have only one modifier; combinations such as `public private` are forbidden. Modifiers are allowed in any scope, including function bodies and module top level, even where they have no practical effect.

Omitting `const` after a modifier implies it:

```goat
public f = func() {};
public const g = func() {};
private var x = 1, y = 2;
```

The first two declarations create public function constants. A modifier applies to every name in a multi-name declaration: both `x` and `y` are private. Modifiers do not change mutability rules: an accessible variable can be read and written, a constant can be read, and an accessible function can be called.

## I.2. Visibility

| Declaration | Access rule |
| --- | --- |
| No modifier | Accessible to all code in its own module, including through `obj.x`; inaccessible to other modules. This is analogous to module/package-private visibility. |
| `public` | Also accessible from other modules. |
| `protected` | Accessible in its object's body and methods, and in descendant constructors and methods. A descendant may be in another module. |
| `private` | Accessible in its object's body and methods; inheritance alone does not grant descendant methods this access. |

Existing single-module programs without modifiers must retain their behavior. Being accessible within a module does not make an unmodified declaration `public`.

The same restrictions apply to name access, `obj.x`, and `obj["x"]`. Indexing does not bypass protection. A violation throws an exception; if static analysis proves the violation unambiguously, compilation fails.

Built-in methods, including `clone`, `instanceOf`, and string and array methods, are public.

## I.3. Functions, Scopes, and Concrete Objects

Access rights depend on where a function is declared and the associated object, not where it is called. A function declared outside an object and subsequently assigned to its property gains no access to its `private` or `protected` members. Moving an existing method to another object does not transfer its rights to that object either: they remain associated with the original object.

Nested functions retain access through closures, including after being returned to outside code. This also applies to methods of a nested object declared within a scope where the private name is accessible:

```goat
var outer = {
    private var secret = 42;
    public inner = {
        public read = func() { return secret; };
    };
};
print(outer.inner.read()); // 42
```

Identical creation code does not give independent objects access to each other's private members:

```goat
const make = func(value) {
    return {
        private var x = value;
        public sumWith = func(other) { return x + other.x; };
    };
};
var a = make(10);
var b = make(20);
print(a.sumWith(b)); // Access error for b.x.
```

For `protected`, the concrete object and its prototypes also matter. Independent objects sharing creation code gain no additional rights. A descendant method can access a protected property of its prototype through an explicit reference such as `Base.x`, as well as through `this`.

## I.4. Inheritance and Property Lookup

An inherited method retains access to its prototype's private properties. Methods declared in the descendant do not gain this access. If a descendant declares its own `x` while the prototype has `private x`, these are independent properties: the prototype's method accesses its own private `x`, and the descendant's method accesses its own. Under multiple inheritance, same-named private properties of different prototypes are also independent.

For `protected`, ordinary lookup rules remain: an own property shadows an inherited one, and the existing search order applies among prototypes.

If lookup finds an inaccessible property, it reports an error rather than continuing to find an accessible same-named property in another prototype. The separate rule above allowing a method to access its own private property remains in effect.

```goat
const Base = { public var x = 1; };
var obj = Base -> { private var x = 2; };
print(obj.x); // Error; public Base.x is not a fallback.
```

## I.5. Module Ownership and Object Mutation

An object belongs to the module containing the code that creates it. If code in B calls a factory from A, the result belongs to A. If code in B creates a descendant of a prototype from A using `Prototype -> { ... }`, the new object belongs to B, and its constructor may declare own properties.

Code in another module cannot add properties to a foreign object. It may modify existing public variables. Protected properties follow the descendant access rules above.

A new property created by a permitted assignment `obj.x = ...` or `obj[key] = ...` receives default visibility. Writing an existing property preserves its modifier and checks access rights.

The restriction on adding properties does not prohibit modifying a received array's elements through indexing or methods such as `push` and `remove`. Protecting a field does not make the returned array immutable. An API author can, for example, return an array copy from a getter.

## I.6. Observation and Copying Requirements

For `for … in`, the key list includes only properties accessible to the calling code. The standard object representation used by `print(obj)` and `str(obj)` also shows only accessible properties. Code with private access may see more properties than outside code.

The following copying requirements have been agreed independently of the still-undecided mechanism for transferring methods:

- `clone()` copies all properties, including those inaccessible to the calling code, and preserves modifiers. The new object belongs to the calling module, which may add properties to it.
- Copied properties retain their declaring module. Thus copying an object from A into B does not expose A's default-visibility fields to B. Object ownership and a property's declaring module are distinct concepts.
- Copying must not expose private or protected data to outside code. A clone is a separate object: a method of the original gains no direct private access to the clone.
- For `flat()`, agreed requirements are filtering by accessibility to the calling code, preserving the modifiers and declaring modules of copied properties, and assigning ownership of the new object to the calling module.

These requirements do not yet form a complete contract for copying functions. In particular, whether transferred methods remain functional depends on resolving the questions in the next section.

## I.7. Native Execution and Open Questions

Native execution must enforce the same restrictions. If access rights cannot be reliably checked at compilation, the function remains in the VM for now; `--native required` reports an error in that case.

There is no final rule for transferring methods, their closures, and their rights through `clone()` and `flat()`. Keeping copied methods functional must be reconciled with private access being tied to a concrete object. Ordinary function assignment does not transfer rights.

A specific contradiction was identified for `flat()`: an external call filters out a private field but may copy a public method that needs it. Reassigning the method's rights to the new object does not restore the missing field. Whether to change the copied data, method binding, or allow an incomplete result is unresolved. None of these alternatives is adopted here.
