# Appendix I. Access Modifiers: Agreed Rules

[Contents](index.md) · [Русский](../ru/appendix-i-access-control.md) · [Previous](appendix-h-decision-history.md)

Status: agreed language design with stages A–F implemented. The examples below describe the complete target behavior; static access proofs still require stage G.

[Detailed examples and edge cases](access-control-examples.md) · [Implementation plan](access-control-implementation-plan.md)

Stage C (#164) enables declaration modifiers in every scope, source regeneration, AST metadata, checked VM execution, and format-11 bytecode. Explicitly modified declarations use lexical cell identities: a captured prototype `x` stays bound to its original cell while `this.x` uses the receiver. Programs without modifiers retain their existing lookup behavior. Until stage G (#168), programs containing modifiers conservatively skip optimization and native inference even with `--optimize all`; access violations are checked at runtime. Stage D (#165) adapts directly copied `clone` methods and their captures, preserving external references and rights. Stage E (#166) transfers the complete flat cell inventory and adapts methods without merging same-named fields. Stage F (#167) filters own-key enumeration and recursive printing by observer permissions, including str, concatenation, format, and Array.toString. The complete protection model still requires stage G static analysis. Module loading remains in #154.

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

## I.6. Observation and Copying Are Different Operations

For `for … in`, the key list includes only properties accessible to the calling code. The standard object representation used by `print(obj)` and `str(obj)` also shows only accessible properties. Code with private access may see more properties than outside code.

Copying does not filter fields by the calling code's access. Both `clone()` and `flat()` transfer hidden data within their copying scope while preserving protection. The earlier proposal to filter fields in `flat()` is superseded: it deprived public methods of necessary private data. Filtering remains a rule for observation: enumeration and standard printing.

The following rules concern user-defined objects; they do not introduce deep copying of arrays or other types.

| Operation property | `clone()` | `flat()` |
| --- | --- | --- |
| Transferred fields | All own fields, including hidden ones | All own and prototype fields, including hidden and shadowed ones, except fields of `Object` |
| Result prototypes | The same prototype objects | Only `Object` |
| Owner of the new object | Calling module | Calling module |
| Field modifiers and declaring modules | Preserved | Preserved |
| Nested objects and arrays | Shared references; no recursive copying | Shared references; no recursive copying |
| Directly copied functions | Adapted to transferred fields | Adapted to transferred fields |

Copying an object from A into B does not expose A's default-visibility fields to B. Object ownership and a property's declaring module are distinct concepts. B may extend the copy with new properties and modify accessible variables. Access modifiers and constness are separate characteristics; this discussion does not revise the existing constness rules of copying operations.

With `clone()`, shared prototype state does not become independent: prototype fields and inherited methods are not copied. Use `flat()` to transfer fields from the prototype chain.

## I.7. Field Identity and the flat Algorithm

A name alone does not identify a field. Two `private x` fields with different declaring owners remain separate after `flat()`. Each owner's method reads its own field. A shadowed nonprivate field is also retained. Captures are one reason this is necessary: a prototype's captured `x` and `this.x` selecting a descendant's field can yield different values.

The agreed algorithm:

1. Build a topological prototype order respecting the existing lookup priority. Each concrete object occurs only once. Process the source object itself once as well.
2. Transfer each object's fields once. Do not transfer `Object`; it remains the result's parent.
3. Preserve each transferred field's provenance, protection, and capture relationships. Public key lookup must select the same source as before `flat()`; selecting the winner must not discard shadowed fields.
4. Transfer functions and their relationships according to the next section.

An arbitrary topological order is insufficient: independent prototypes must keep their previous priority. Deduplicate by object identity, not name, field values, or shared creation code. In a diamond, the shared ancestor and its field are transferred once; methods of both branches use one new cell. Distinct ancestors with identical field names are not merged.

Internally retaining field provenance does not restore old prototypes to the result's chain. They do not become result parents or appear in `instanceOf` solely due to internal metadata. `Object` methods remain available through the sole parent; copying them into own properties is unnecessary.

The prototype traversal follows actual inheritance edges. A constructor's lexical fallback is not an inherited object: its variables remain external captures rather than becoming own fields of the flat result. Internal method environments retain these fallbacks separately; they do not add parents to the result or change `instanceOf`. The previous ordinary lookup priority is retained among transferred fields.

A file-local declaration identifier alone is also insufficient: two calls to one factory use the same bytecode identifiers but create different cells. After flattening, each method's concrete capture takes priority over scanning the merged storage by identifier. Repeated `flat()` and `clone()` rebind these captures and internal environments again.

## I.8. Methods, Captures, and Rights During Copying

These rules apply to functions directly transferred as property values. Function code may remain shared, but its environment and rights must be adapted without changing the original function.

| Function relationship | Copying result |
| --- | --- |
| Named capture of a transferred field | Refers to the corresponding new cell |
| Access through `this` | Uses the call receiver and transferred private/protected access domains |
| Capture of an external variable | Remains shared with the original |
| Explicit external reference to the original, such as `A.x` | Still addresses A; previously held access is retained |
| Function inside a nested object | Is not adapted: the nested object stays shared |
| Function lacking a private right | Does not gain it through copying |

Thus copied methods can modify a private field of the copy both through `this.x` and through captured name `x`. Updating the copy's cell does not update the original's cell; if a cell holds a nested mutable object, that object itself remains shared.

New rights correspond only to transferring existing rights to copied domains. An outside function stored in a property does not become a privileged method. The original method gains no access to the copy's private fields. However, a copied method retains previously permitted access to the original through an explicit external reference. This qualifies the earlier overly broad statement that a copy can never read the original's private fields.

Two properties that referenced one function reference one adapted function in the result. Adapt once per original function within one copying operation; do not merge distinct functions with identical code. Shared captures of multiple methods must remain shared within the copy. Ordinary method assignment to another object does not perform this adaptation.

This is shallow copying with adaptation of directly transferred functions, not recursive cloning of the object graph. Nested objects, their methods, and external variables can still connect the result to the original. Calling `flat()` does not promise complete independence.

## I.9. Native Execution and Status

Native execution must enforce the same restrictions. If access rights cannot be reliably checked at compilation, the function remains in the VM for now; `--native required` reports an error in that case. Copying must not reuse a native specialization if its environment and access assumptions are no longer proven.

The identified filtering and copying contradictions are resolved by the rules above. The actual status of each stage is in the [implementation plan](access-control-implementation-plan.md). Declaration, clone, flat, and filtering examples now execute in the VM; modules and compile-time access diagnostics remain acceptance criteria for later stages. Adapted functions do not retain native specializations whose environment assumptions may have changed.
