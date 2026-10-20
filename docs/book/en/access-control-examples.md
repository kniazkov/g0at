# Access Modifiers: Examples and Edge Cases

[Contents](index.md) · [Русский](../ru/access-control-examples.md) · [Rules](appendix-i-access-control.md) · [Plan](access-control-implementation-plan.md)

These examples describe the agreed design. Stages A–D implement declarations, VM access checks, and clone method/capture adaptation; flat adaptation, observation filtering, module loading, and static access diagnostics still belong to later stages. Each example is independent; results are described in prose, and `print` calls do not imply automatic newlines. Negative cases are checked separately, both statically and with dynamically unknown receivers. All examples use the standard built-in `clone` and `flat`, not user overrides.

<a id="ac-01"></a>

## AC-01. Declarations and shorthand

```goat
public f = func() { return 1; };
public const g = func() { return 2; };
private var x = 3, y = 4;
protected left = 5, right = 6;
```

f and g are public function constants; x and y are private variables; left and right are protected constants. Modifier syntax is permitted in any scope.

<a id="ac-02"></a>

## AC-02. Invalid forms

```goat
public private var x = 1;
private obj.x = 2;
protected obj["x"] = 3;
```

Each line is a separate negative example. Two modifiers are forbidden, and a modifier cannot precede property assignment. Assignment to an existing property does not change its protection.

<a id="ac-03"></a>

## AC-03. Object body and both access forms

```goat
var obj = {
    private var x = 42;
    print(x);
    public read = func() { return this["x"]; };
};
print(obj.read());
print(obj["x"]);
```

The first two print calls produce 42. The final access is forbidden, as is obj.x. Test the last line separately: static diagnostics may prevent execution of the entire program.

<a id="ac-04"></a>

## AC-04. An outside function gains no privilege

```goat
const external = func() { return this.x; };
var obj = {
    private var x = 10;
    public read = external;
};
var copy = obj.clone();
print(copy.read());
```

Access error. Neither storing external in a property nor cloning grants rights to private x. The same result is expected with flat().

<a id="ac-05"></a>

## AC-05. Moving a method by ordinary assignment

```goat
var a = {
    private var x = 10;
    public read = func() { return this.x; };
};
var b = {
    private var x = 20;
};
b.read = a.read;
print(b.read());
```

Access to b's private x is denied. Moving a method reference does not adapt its rights. This differs from the special method adaptation performed by clone/flat.

<a id="ac-06"></a>

## AC-06. Independent objects from one template

```goat
const make = func(value) {
    return {
        private var x = value;
        public sumWith = func(other) { return x + other.x; };
    };
};
var a = make(10);
var b = make(20);
print(a.sumWith(b));
```

Access to b.x is denied. Sharing an AST or factory body is not identity of the private domain owner. Protected access likewise does not extend to an independent object.

<a id="ac-07"></a>

## AC-07. A closure outlives its method call

```goat
var obj = {
    private var x = 42;
    public reader = func() {
        return func() { return x; };
    };
};
var read = obj.reader();
print(read());
```

42. Returning a function does not revoke its existing access. Outside code can call the supplied function but gains no direct access to obj.x.

<a id="ac-08"></a>

## AC-08. A nested object's method

```goat
var outer = {
    private var secret = 42;
    public inner = {
        public read = func() { return secret; };
    };
};
print(outer.inner.read());
```

42: the capture is permitted at the declaration site. This does not mean shallow copying outer also copies inner (see AC-16).

<a id="ac-09"></a>

## AC-09. Descendant constructor and methods

```goat
const Base = {
    protected var x = 10;
    private var secret = 7;
    public readSecret = func() { return secret; };
};
var child = Base -> {
    public fromBody = x;
    public readBase = func() { return Base.x; };
};
print(child.fromBody);
print(child.readBase());
print(child.readSecret());
```

10, 10, 7. The descendant constructor and method have protected access; inherited readSecret retains Base's private right. A new descendant method directly reading secret must be rejected. Protected access also works for a descendant in another module.

<a id="ac-10"></a>

## AC-10. An inaccessible field stops lookup

```goat
const Base = { public var x = 1; };
var obj = Base -> { private var x = 2; };
print(obj.x);
```

An error, not 1. Lookup does not bypass a forbidden field to find an accessible prototype field. This also holds after flat().

<a id="ac-11"></a>

## AC-11. flat retains hidden data

```goat
var obj = {
    private var x = 42;
    public getX = func() { return this.x; };
};
var copy = obj.flat();
print(copy.getX());
```

42. The field is transferred regardless of the caller's rights, but external copy.x and copy["x"] remain forbidden. Printing and external enumeration do not reveal key x.

<a id="ac-12"></a>

## AC-12. Two private x fields in a three-object chain

```goat
const A = {
    private var x = 10;
    public getA = func() { return this.x; };
};
const B = A -> {
    private var x = 20;
    public getB = func() { return this.x; };
};
var obj = B -> {
    public sum = func() { return this.getA() + this.getB(); };
};
var copy = obj.flat();
print(copy.getA());
print(copy.getB());
print(copy.sum());
```

10, 20, 30. The copy has two independent private fields despite their identical key. Merging by string x is invalid. Provenance is needed for rights and binding, but A and B do not become the copy's prototypes.

<a id="ac-13"></a>

## AC-13. Shadowed protected field and named capture

```goat
const A = {
    protected var x = 10;
    public getCaptured = func() { return x; };
    public getCurrent = func() { return this.x; };
};
var obj = A -> { protected var x = 20; };
var copy = obj.flat();
print(copy.getCaptured());
print(copy.getCurrent());
```

10 and 20, as before flat(). The capture refers to A's concrete cell, whereas this.x performs lookup including the descendant. The internal cell holding 10 survives even though ordinary lookup selects 20.

<a id="ac-14"></a>

## AC-14. Multiple methods share the new captured field

```goat
var obj = {
    private var x = 10;
    public getX = func() { return x; };
    public setX = func(value) { x = value; };
    public viaThis = func() { return this.x; };
};
var copy = obj.clone();
copy.setX(20);
print(obj.getX());
print(copy.getX());
print(copy.viaThis());
```

10, 20, 20. All three copied methods refer to one new x cell. Original functions and the original cell are unchanged. The same is expected for flat().

<a id="ac-15"></a>

## AC-15. An external capture stays shared

```goat
var counter = 0;
var obj = {
    private var x = 10;
    public next = func() {
        counter += 1;
        x += 1;
        return x;
    };
};
var copy = obj.clone();
print(copy.next());
print(obj.next());
print(counter);
```

11, 11, 2. Each object version has its own x cell, but both functions modify one external counter. Neither clone nor flat copies the surrounding scope.

<a id="ac-16"></a>

## AC-16. The shallow-copy boundary

```goat
var obj = {
    private var x = 10;
    public setX = func(value) { x = value; };
    public inner = {
        public getX = func() { return x; };
    };
};
var copy = obj.clone();
copy.setX(20);
print(copy.inner == obj.inner);
print(copy.inner.getX());
obj.setX(30);
print(copy.inner.getX());
```

true, 10, 30. inner is shared; its function is not directly transferred and still reads the original's x. A shared inner cannot be rebound only for copy. flat() does not recursively traverse nested objects either.

<a id="ac-17"></a>

## AC-17. An explicit original reference and retained rights

```goat
const A = {
    private var x = 10;
    public setLocal = func(value) { x = value; };
    public local = func() { return x; };
    public original = func() { return A.x; };
};
var copy = A.clone();
copy.setLocal(20);
print(copy.local());
print(copy.original());
```

20 and 10. The external capture A is not replaced with the copy; the method retains its original right to read A.x. This grants the original method no right to read copy's private fields. The same distinction holds with protected and with flat().

<a id="ac-18"></a>

## AC-18. The original gains no rights over its clone

```goat
var obj = {
    private var x = 10;
    public readOther = func(other) { return other.x; };
};
var copy = obj.clone();
print(obj.readOther(copy));
```

Access error. Allowing a copied method to retain an existing explicit reference to the original is not symmetric and does not expose the new object to the original.

<a id="ac-19"></a>

## AC-19. A shared prototype under clone

```goat
const Base = {
    private var x = 0;
    public next = func() { x += 1; return x; };
};
var obj = Base -> {};
var copy = obj.clone();
print(obj.next());
print(copy.next());
```

1 and 2. Here clone transfers neither x nor next: they belong to the shared Base. Replacing clone with flat yields 1 and 1: flat transfers the field and function into a new domain.

<a id="ac-20"></a>

## AC-20. Two names for one function

```goat
var obj = {
    private var x = 10;
    public f = func() { return x; };
    public g = f;
};
var copy = obj.clone();
print(copy.f == copy.g);
print(copy.f == obj.f);
print(copy.g());
```

true, false, 10. One original function requires one adapted function in this operation. Its environment differs from the original. The same rule applies to flat(); independent functions with identical code are not merged.

<a id="ac-21"></a>

## AC-21. Diamond: one cell for the shared ancestor

```goat
const A = {
    private var x = 0;
    public inc = func() { x += 1; return x; };
    public read = func() { return x; };
};
const Left = A -> {
    public viaLeft = func() { return this.inc(); };
};
const Right = A -> {
    public viaRight = func() { return this.read(); };
};
var obj = [Left, Right] -> {};
var copy = obj.flat();
print(copy.viaLeft());
print(copy.viaRight());
print(A.read());
```

1, 1, 0. A is processed once, and both branches use one copied cell. The original cell is unchanged. This checks deduplication by object identity.

<a id="ac-22"></a>

## AC-22. Lookup priority and the sole parent

```goat
const Left = { public var x = 1; };
const Right = { public var x = 2; };
var obj = [Left, Right] -> {};
var copy = obj.flat();
print(copy.x == obj.x);
print(copy.instanceOf(Object));
print(copy.instanceOf(Left));
print(copy.instanceOf(Right));
```

true, true, false, false. The test deliberately compares against existing lookup: an arbitrary topological order must not select a different winner. instanceOf is inherited from Object rather than copied from it.

<a id="ac-23"></a>

## AC-23. Filtered observation after complete copying

```goat
var obj = {
    private var secret = 42;
    public var shown = 7;
    public read = func() { return secret; };
};
var copy = obj.flat();
for (key in copy) print(key);
print(str(copy));
print(copy.read());
```

External enumeration does not yield secret, and the standard string representation does not expose its field or value 42. read() returns 42. Exact function rendering and print order are not specified here. An own method with private access may observe secret.

<a id="ac-24"></a>

## AC-24. An array remains mutable

```goat
var obj = {
    private var items = [1];
    public getItems = func() { return items; };
    public getCopy = func() { return items.clone(); };
};
var shared = obj.getItems();
shared.push(2);
var separate = obj.getCopy();
separate.push(3);
print(obj.getItems());
```

The original array contains [1, 2]. Field privacy does not prevent modifying an object returned by an authorized method, even from another module. An array copy only isolates changes to its own elements; it is not a deep copy of nested values.

<a id="ac-25"></a>

## AC-25. Factory and object module ownership

Module A:

```goat
public make = func() {
    return {
        public var x = 1;
        var internal = 7;
    };
};
```

Module B:

```goat
var obj = A.make();
obj.x = 2;
var copy = obj.clone();
copy.extra = 3;
var child = obj -> { var own = 4; };
```

The first block is module A; the second is module B. Assume B already has a public reference A; no import syntax is specified. obj belongs to A, so a separate obj.extra = 3 attempt is forbidden. copy and child belong to B. B cannot read obj.internal or copy.internal: the field's declaring module remains A.

<a id="ac-26"></a>

## AC-26. A copied method retains its module

Module A:

```goat
public obj = {
    var internal = 7;
    public read = func() { return internal; };
};
```

Module B:

```goat
var copy = A.obj.flat();
copy.extra = 1;
print(copy.read());
```

read() yields 7. The result belongs to B, but transferred read remains code from module A and can read the transferred field declared in A. External copy.internal in B remains forbidden. Changing object ownership does not change the module of its functions or every field.

<a id="ac-27"></a>

## AC-27. Protected access across module boundaries

Module A:

```goat
public Base = {
    protected var x = 10;
    private var secret = 20;
};
```

Module B:

```goat
var child = A.Base -> {
    public read = func() { return A.Base.x; };
};
print(child.read());
```

10. Arbitrary outside code in B cannot read protected x, but the descendant method may read A.Base.x. An analogous read of A.Base.secret by a new descendant method is forbidden.

<a id="ac-28"></a>

## AC-28. Public access does not remove constness

Module A:

```goat
public obj = {
    public var writable = 1;
    public fixed = 2;
};
```

Module B:

```goat
A.obj.writable = 3;
print(A.obj.fixed);
```

The write is allowed and the read yields 2. A separate A.obj.fixed = 4 attempt is forbidden by constness. Adding A.obj.newField = 5 is forbidden by A's object ownership. These are distinct rejection reasons and diagnostics must distinguish them.


