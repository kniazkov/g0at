# Prototype construction: `->`

```goat
const Vector = {
    var x = 0;
    var y = 0;
    const length = func() {
        return sqrt(this.x * this.x + this.y * this.y);
    };
};
var v = Vector -> { x = 3; y = 4; };
println(v.length()); // 5.0
println(Vector.x);   // 0
```

`prototype -> { constructor }` creates a fresh ordinary object inheriting from the prototype, validates and runs its ancestor init chain, then runs the right-hand block in that object's scope and returns the object. The right operand must be a literal block; `{}` is a valid empty constructor. `this` in the right-hand block refers to the new object; method calls use their receiver. The enclosing environment remains available through the same prototype-based lexical mechanism as ordinary object blocks.

Assignments in the constructor create or update the new object's own properties. They do not overwrite prototype properties. The same rule applies to later named/indexed writes to the derived object and to inherited methods assigning through `this`. Unqualified names and captured locals retain lexical lookup; they are not redirected to the receiver. An inherited constant may be shadowed; a constant declared on the new object itself remains protected. An assignment without `var` or `const` in a constructor is a runtime property write, not an implicit constant declaration.

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

The AST has separate `inheritance` and `prototype list` nodes. `INHERIT` consumes the evaluated prototypes once, enters the new scope, and runs the validated init snapshot; the right-hand block then executes there and `LEAVE` returns the result. Object operations retain VM semantics, including when a native continuation drives VM instructions. A native object representation is not introduced.


## Init and method contract

The snapshot visits only real ancestors, not enclosing lexical scopes, in reverse
lookup order: bases before derived objects, independent `[A, B]` as B then A.
Each ancestor object occurs once, including diamonds and repeated prototypes;
one function on different ancestors runs multiple times. Only own init fields are
selected. Missing init is skipped; any present non-function, including null or a
callable object, fails validation before the first call. The snapshot retains the
concrete functions and receiver for GC. Replacing init affects the next construction.

All selected functions receive the same `this` and zero actual arguments. Missing
formal parameters are null and `arguments` is empty. Return from init exits only that
method and discards its value. Return from the right-hand block still exits the
surrounding function. An init declared in that block is for future descendants.
An exception preserves its trace, stops later init and skips the block. There is no
rollback: an externally saved partial receiver remains usable with prior writes.

Automatic selection ignores the access modifier of init, but its body keeps ordinary
method rights. Manual `obj.init()` checks access, calls one normally selected method,
and returns its value. Literals and clone do not automatically invoke init. Neither
inheritance nor initialization performs deep copying: new arrays allocated in init
are independent; assigning a shared array deliberately shares it.

For every dotted or indexed method call, including inherited methods, `this` is the
receiver. A nested object block has its own `this`; leaving it restores the receiver.
An ordinary `f()` call has its own call scope, including a function extracted from
an object. Capture `self = this` explicitly to retain an outer receiver. Unqualified
locals and captures remain lexical. This intentionally changes historical method
`this`; old receiver-less behavior is not preserved.

## Execution modes and saved programs

The #533 mode fixtures compare exact output and constructor throw origins with
optimization none/all in VM and saved GBIN. Native auto/required use optimize all;
reports assert actual entries and no replay. Required means successful native
preparation, not native execution of every instruction. Object operations may use
VM fallback or continuations. Imported-module tests cover private init selection,
body rights, source and persisted module execution, and auto fallback. A library
factory creates library-owned receivers; direct construction in another module
still raises FOREIGN_OBJECT if init tries to extend that foreign receiver.

This series does not change the GBIN layout/version or rewrite loaded instruction
bytes. Existing valid images use the current CALLM/INHERIT semantics when executed:
format readability does not promise historical method-this or init behavior.
Regenerate persisted artifacts when adopting the new language semantics. Tests
compare archive bytes before/after runs; component tests validate linked images,
assert empty stacks and compare instruction images after execution. Loader validation
is not a complete proof of arbitrary bytecode stack depth or control flow.

Executable evidence: functional `method_this_*`, `init_*`; `native.operators.init-modes`
and `init-trace`; `integration.module_multifile_access.init-*`; and
`component.native_continuation.init-*`. See the paired book chapters 10 and 25,
VM instruction appendix C, and access appendix I.

# Наследование через `->`

`прототип -> { конструктор }` создаёт новый обычный объект, наследует прототип, проверяет и вызывает цепочку init предков, затем исполняет правый блок в области нового объекта и возвращает его. Справа обязателен блок; пустой `{}` допустим. `this` в правом блоке обозначает новый объект; при вызове метода он обозначает получателя. Внешняя область остаётся доступна через тот же механизм прототипов, что и у обычных объектных блоков.

Присваивание в конструкторе создаёт или меняет собственное свойство нового объекта. Прототип не изменяется. Это правило сохраняется для последующих записей через точку, индекс и унаследованные методы, пишущие через `this`. Имена без `this` и захваченные локальные переменные сохраняют лексический поиск и не перенаправляются на получателя. Унаследованную константу можно перекрыть собственным свойством; собственную константу изменять нельзя. Присваивание без `var` или `const` здесь — динамическая запись свойства, а не неявное объявление константы.

Свойства не копируются: пока свойство не перекрыто, изменения прототипа видны потомку. Изменяемые значения, например массивы, остаются общими. `clone()` копирует собственные свойства и сохраняет прототипы и правила присваивания.

`[A, B,] -> { ... }` разбирается как отдельная конструкция со списком прототипов, без создания массива во время исполнения. Последняя запятая допустима. Прототипы вычисляются один раз слева направо, затем исполняется конструктор. Пустой список допустим и сохраняет внешнюю область. Используется существующий топологический порядок: потомок раньше предка, общие предки ромба учитываются один раз, в независимых ветвях приоритет у левого прототипа. Поэтому даже в `[Base, Derived]` свойства `Derived` находятся раньше свойств его предка `Base`.

Можно передать готовый массив: `parents -> {}`. Его текущие элементы становятся прототипами; последующее изменение массива не меняет список предков объекта. Для наследования от самого массива используется `[array] -> {}`. Прототипом может быть любое значение Goat, но результат всегда является обычным объектом, а не копией внутреннего представления типа прототипа.

Оператор левоассоциативен, его приоритет ниже логических операций и выше присваивания. Для немедленного доступа к результату нужны скобки: `(Vector -> { x = 3; }).x`. Исключения из конструктора распространяются обычным образом; `return` возвращает из окружающей функции. Уже выполненные побочные эффекты не отменяются.

В AST выделены узлы `inheritance` и `prototype list`. Байткод `INHERIT` однократно снимает прототипы со стека, открывает область объекта и вызывает проверенный снимок init; затем выполняется правый блок, `LEAVE` возвращает объект. Операции объектов сохраняют семантику VM, в том числе под управлением native continuation; нативное представление объектов не вводится.


## Контракт init и методов

Снимок обходит только реальных предков, не внешние лексические области, в порядке,
обратном поиску: базовые перед производными, независимые `[A, B]` — B, затем A.
Каждый объект-предок встречается один раз, включая ромбы и повторы; одна функция
у разных предков вызывается несколько раз. Выбираются только собственные поля init.
Отсутствующее поле пропускается; любое присутствующее не-функциональное значение,
включая null и вызываемый объект, вызывает ошибку до первого вызова. Снимок удерживает
конкретные функции и получателя для GC. Замена init влияет на следующее создание.

Все выбранные функции получают один `this` и ноль фактических аргументов.
Незаполненные параметры равны null, `arguments` пуст. Return из init завершает только
метод и отбрасывает значение. Return из правого блока по-прежнему выходит из
окружающей функции. Init, объявленный в этом блоке, предназначен будущим потомкам.
Исключение сохраняет трассировку, прекращает цепочку и пропускает блок. Отката нет:
сохранённый снаружи частичный получатель остаётся рабочим с предыдущими записями.

Автоматический выбор игнорирует модификатор init, но тело сохраняет обычные права
метода. Ручной `obj.init()` проверяет доступ, вызывает один найденный обычным поиском
метод и возвращает значение. Литералы и clone не запускают init автоматически.
Наследование и инициализация не выполняют глубокое копирование: новые массивы внутри
init независимы; присваивание общего массива намеренно разделяет его.

Во всех вызовах метода через точку или индекс, включая унаследованные методы,
`this` — получатель. Вложенный объектный блок имеет свой `this`; выход восстанавливает
получателя. Обычный `f()` имеет свою область вызова, в том числе после извлечения
функции из объекта. Для сохранения внешнего получателя явно захватывайте `self = this`.
Локальные имена и захваты остаются лексическими. Это намеренное изменение
исторической семантики this в методах; прежнее поведение не сохраняется.

## Режимы исполнения и сохранённые программы

Тесты режимов #533 сравнивают точный вывод и исходную точку исключения конструктора
с оптимизацией none/all в VM и сохранённом GBIN. Native auto/required используют
optimize all; отчёты проверяют реальные входы и отсутствие повторов. Required означает
успешную подготовку native, а не нативное исполнение каждой инструкции. Операции
объектов могут использовать VM fallback или продолжения. Тесты импортированных
модулей проверяют закрытый init, права тела, исходные и сохранённые модули и auto fallback.

Серия не меняет формат/версию GBIN и не переписывает загруженные инструкции.
Существующие допустимые образы исполняются с текущей семантикой CALLM/INHERIT:
читаемость формата не обещает прежнюю семантику this или init. При переходе на новую
семантику языка пересоберите сохранённые артефакты. Тесты сравнивают архив до/после
исполнения; компонентные тесты валидируют слинкованные образы, проверяют пустой стек
и неизменность образа инструкций. Валидация загрузчика не доказывает корректность
глубины стека и потока управления произвольного байткода.

Исполняемые проверки: functional `method_this_*`, `init_*`; `native.operators.init-modes`
и `init-trace`; `integration.module_multifile_access.init-*`;
`component.native_continuation.init-*`. См. парные главы книги 10 и 25,
приложение C об инструкциях VM и приложение I о доступе.
