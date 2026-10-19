# Prototype construction: `->`

```goat
const Vector = {
    var x = 0;
    var y = 0;
    const length = func() {
        return sqrt(x * x + y * y);
    };
};
var v = Vector -> { x = 3; y = 4; };
println(v.length()); // 5.0
println(Vector.x);   // 0
```

`prototype -> { constructor }` creates a fresh ordinary object inheriting from the prototype, runs the block in that object's scope, and returns the object. The right operand must be a literal block; `{}` is a valid empty constructor. `this` follows Goat's scope semantics and refers to the constructor's object. The enclosing environment remains available through the same prototype-based lexical mechanism as ordinary object blocks.

Assignments in the constructor create or update the new object's own properties. They do not overwrite prototype properties. The same rule applies to later named/indexed writes to the derived object and to inherited methods assigning through its scope. An inherited constant may be shadowed; a constant declared on the new object itself remains protected. An assignment without `var` or `const` in a constructor is a runtime property write, not an implicit constant declaration.

Inheritance does not copy properties. Unshadowed reads continue to see changes to the prototype; inherited mutable values, such as arrays, are shared. `clone()` copies the derived object's own properties and retains its prototypes and assignment behavior.

```goat
const Position = { var x = 0; var y = 0; };
const Named = { var name = "unnamed"; };
var item = [Position, Named,] -> { name = "point"; x = 7; };
println(item.instanceOf(Position)); // true
println(item.instanceOf(Named));    // true
```

The bracketed form is a separate prototype-list syntax, with an optional trailing comma. Its operands evaluate once, left to right, before the constructor. It emits no runtime array allocation. Empty lists are allowed and retain the enclosing environment. The existing topological lookup visits descendants before ancestors, removes duplicates in diamonds, and gives the left prototype precedence for independent branches. Thus `[Base, Derived]` still resolves a property on `Derived` before its ancestor `Base`.

A computed array on the left, such as `parents -> {}`, supplies its current elements as prototypes. Subsequent changes to that array do not alter the object's prototype list. To inherit from an array object itself, use `[array] -> {}`. Every Goat value may be a prototype; the result is still an ordinary object, not an instance of the prototype's internal C representation.

`->` associates left to right and binds below logical operators but above assignment. Use parentheses to access the result immediately: `(Vector -> { x = 3; }).x`. A constructor exception propagates normally; a `return` inside it returns from the surrounding function. Earlier side effects are not rolled back.

The AST has separate `inheritance` and `prototype list` nodes. `INHERIT` consumes the evaluated prototypes and enters the new scope; the constructor executes there and `LEAVE` returns the result. Construction and closures capturing its dynamic name lookup execute on the VM; native object representation is not introduced.

# Наследование через `->`

`прототип -> { конструктор }` создаёт новый обычный объект, наследует прототип, исполняет блок в области нового объекта и возвращает его. Справа обязателен блок; пустой `{}` допустим. `this` обозначает объект конструктора согласно правилам областей видимости Goat. Внешняя область остаётся доступна через тот же механизм прототипов, что и у обычных объектных блоков.

Присваивание в конструкторе создаёт или меняет собственное свойство нового объекта. Прототип не изменяется. Это правило сохраняется для последующих записей через точку, индекс и унаследованные методы. Унаследованную константу можно перекрыть собственным свойством; собственную константу изменять нельзя. Присваивание без `var` или `const` здесь — динамическая запись свойства, а не неявное объявление константы.

Свойства не копируются: пока свойство не перекрыто, изменения прототипа видны потомку. Изменяемые значения, например массивы, остаются общими. `clone()` копирует собственные свойства и сохраняет прототипы и правила присваивания.

`[A, B,] -> { ... }` разбирается как отдельная конструкция со списком прототипов, без создания массива во время исполнения. Последняя запятая допустима. Прототипы вычисляются один раз слева направо, затем исполняется конструктор. Пустой список допустим и сохраняет внешнюю область. Используется существующий топологический порядок: потомок раньше предка, общие предки ромба учитываются один раз, в независимых ветвях приоритет у левого прототипа. Поэтому даже в `[Base, Derived]` свойства `Derived` находятся раньше свойств его предка `Base`.

Можно передать готовый массив: `parents -> {}`. Его текущие элементы становятся прототипами; последующее изменение массива не меняет список предков объекта. Для наследования от самого массива используется `[array] -> {}`. Прототипом может быть любое значение Goat, но результат всегда является обычным объектом, а не копией внутреннего представления типа прототипа.

Оператор левоассоциативен, его приоритет ниже логических операций и выше присваивания. Для немедленного доступа к результату нужны скобки: `(Vector -> { x = 3; }).x`. Исключения из конструктора распространяются обычным образом; `return` возвращает из окружающей функции. Уже выполненные побочные эффекты не отменяются.

В AST выделены узлы `inheritance` и `prototype list`. Байткод `INHERIT` снимает прототипы со стека и открывает область нового объекта; конструктор выполняется в ней, `LEAVE` возвращает объект. Конструкция и замыкания с динамическим поиском имён исполняются на VM; нативное представление объектов здесь не вводится.
