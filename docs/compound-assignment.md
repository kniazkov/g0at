# Compound assignments and updates / Составные присваивания и обновления

## English

The operators `+=`, `-=`, `*=`, `/=`, `%=`, `**=`, `&=`, `|=`, `^=`, `<<=` and `>>=`
read a target, apply the corresponding ordinary binary operation, and store its
result. The target may be a variable, a named property, or an indexed
property/element. An object-valued old value uses its ordinary operator handler,
such as `"+"` for `+=`. A separate `"+="` property is never called. Plain variable
assignment does not call an `"="` property.

### One target and ordered effects

For `container[index()] += amount()`:

1. Evaluate and retain the container and each index once, left to right.
2. Read the old value through `"[]"`, or the ordinary indexed fallback.
3. Evaluate `amount()`.
4. Apply `+` to the saved old value and the RHS. The handler belongs to the old
   value, not to the container, and is selected after the RHS.
5. Select `"[]="` on the retained container immediately before the write. Pass
   the retained indices followed by the new value. Return that new value,
   ignoring the setter's ordinary return.

Reassigning the container variable or changing the same element during the RHS
never replaces the retained address or old value. Replacing the setter during
the getter, RHS or arithmetic handler affects the current write. A setter may
also be installed during those stages. User-object setter lookup, access and
function checks are not performed before the read or RHS.

An exception from the getter prevents the RHS and operation. An exception from
the RHS or operation prevents the final write. A setter exception occurs after
the earlier effects, which remain visible: there is no rollback. In particular,
an arithmetic handler may mutate the old object before a later setter fails.
A present non-function setter raises `INVALID_OPERATION` at the write stage;
only an absent handler selects ordinary indexed writing.

A constant variable fails its write check before the RHS and operator handler.
Named-property and specialized array checks also retain their existing early
write checks. These checks do not replace the late user-object setter selection.
Dot access never invokes `"[]"` or `"[]="`; it can still read an object value
whose arithmetic or update operator is overloaded. Ordinary property/prototype
write and access rules are unchanged.

Empty or multiple indices are valid when accepted by the handlers: `a[] += b`
reads with zero arguments, adds `b`, and writes with only the new value.
`a[i,j] += b` passes the same two indices to both handlers. Built-in fallback
keeps its ordinary arity and bounds rules.

Compound assignment has the priority and right associativity of `=`.
For example, `a += b *= 2` means `a += (b *= 2)`. With `x` initially 10,
`x += (x = 5)` returns and stores 15. Chained indexed assignments likewise
propagate the assigned value, not a setter return.

### Prefix and postfix `++` / `--`

Both forms support the same variable, property and index targets. The target is
retained once and the current value is read once. A user object invokes its
separate `"++"` or `"--"` function with that value as `this` and no arguments.
There is no fallback to `"+"` or `"-"` with one. A missing or non-function update
handler raises `INVALID_OPERATION`.

The handler's result is written to the retained target. Prefix returns the new
value; postfix returns the saved old value. A saved object is the same reference,
not a deep copy, so mutations made by the handler remain visible through the
postfix result. The final index setter is selected after the update handler and
its normal return is ignored. Target/read/operator/write exceptions preserve
completed effects and prevent the remaining stages.

```goat
const storage = [7];
const box = {};
box["[]"] = func() { return storage[0]; };
box["[]="] = func(value) { storage[0] = value; return 999; };
println(box[] += 2); // 9
println(box[]++);    // 9, stores 10
println(++box[]);    // 11, stores 11
```

### Execution scope

The VM uses existing checks, indexed/property loads and stores, `DUP`, `DUPN`,
`ROTATE`, and arithmetic/update instructions. No opcode or format version is
added; executable bytecode is not rewritten and no old-bytecode migration path
is introduced. Operator/getter/setter continuations retain operands through
nested calls, collection, `finally` and exception unwinding.

Native compilation retains the existing proven local numeric/string paths.
Indexed/property and user-handler execution may use the VM; actual native
operator integration remains [#503](https://github.com/kniazkov/g0at/issues/503).
Enabling native mode alone does not prove native handler execution. See also the
[index handlers](operator-index.md) and [arithmetic handlers](operator-arithmetic.md).

## Русский

Операторы `+=`, `-=`, `*=`, `/=`, `%=`, `**=`, `&=`, `|=`, `^=`, `<<=` и `>>=`
читают цель, применяют соответствующую обычную бинарную операцию и записывают
результат. Целью может быть переменная, свойство через точку или индексный доступ.
Если прочитанное значение является пользовательским объектом, вызывается его
обычный обработчик, например `"+"` для `+=`. Отдельное свойство `"+="` не
вызывается. Простое присваивание переменной не вызывает свойство `"="`.

### Одна цель и порядок эффектов

Для `container[index()] += amount()`:

1. Один раз вычислить и сохранить контейнер и каждый индекс слева направо.
2. Прочитать старое значение через `"[]"` либо стандартную индексацию.
3. Вычислить `amount()`.
4. Применить `+` к сохранённому старому значению и правой части. Обработчик
   выбирается после правой части у старого значения, а не у контейнера.
5. Непосредственно перед записью выбрать `"[]="` у сохранённого контейнера,
   передать сохранённые индексы и новое значение последним. Вернуть новое
   значение, отбросив обычный результат setter.

Переназначение переменной контейнера или изменение элемента в правой части не
меняет сохранённые адрес и старое значение. Замена setter во время getter,
правой части или арифметического обработчика влияет на текущую запись. Setter
может быть установлен на любом из этих этапов. Поиск, проверка доступа и вида
пользовательского setter не выполняются до чтения или правой части.

Исключение getter предотвращает правую часть и операцию. Исключение правой части
или операции предотвращает итоговую запись. Ошибка setter возникает после
предыдущих эффектов; они сохраняются без отката. Например, арифметический
обработчик может изменить прочитанный объект, после чего setter выбросит ошибку.
Нефункциональный setter вызывает `INVALID_OPERATION` на этапе записи; только
отсутствие обработчика включает стандартную индексную запись.

Запрет записи константной переменной проверяется до правой части и обработчика
оператора. Именованные свойства и специализированный доступ к массивам также
сохраняют прежние ранние проверки записи. Эти проверки не заменяют поздний
выбор пользовательского setter. Точка не вызывает `"[]"` или `"[]="`, но может
прочитать объект с перегруженной арифметической операцией или `++`/`--`.
Обычные правила записи свойств, прототипов и прав доступа сохраняются.

Пустой и множественный списки индексов допустимы для обработчиков: `a[] += b`
читает без аргументов, прибавляет `b` и передаёт на запись только новое значение.
`a[i,j] += b` передаёт обоим обработчикам те же два индекса. Стандартная индексация
сохраняет ограничения количества индексов и границ.

Приоритет и правая ассоциативность совпадают с `=`: `a += b *= 2` означает
`a += (b *= 2)`. При начальном `x = 10` выражение `x += (x = 5)` возвращает
и сохраняет 15. Цепные индексные присваивания также передают записанное значение,
а не возвращённое setter.

### Префиксные и постфиксные `++` / `--`

Обе формы поддерживают переменные, свойства и индексы. Цель сохраняется один раз,
текущее значение читается один раз. Пользовательский объект вызывает отдельную
функцию `"++"` или `"--"` без аргументов, с прочитанным значением как `this`.
Подстановки `"+"` или `"-"` с единицей нет. Отсутствие или нефункциональное
значение обработчика вызывает `INVALID_OPERATION`.

Результат обработчика записывается в сохранённую цель. Префикс возвращает новое
значение, постфикс возвращает старое. Старый объект сохраняется по той же ссылке,
без глубокого копирования, поэтому мутации обработчика видны через постфиксный
результат. Индексный setter выбирается после обработчика обновления; его обычный
результат отбрасывается. Исключения вычисления цели, чтения, операции и записи
сохраняют завершённые эффекты и прекращают последующие этапы.

В приведённом выше примере `box[] += 2` возвращает 9, `box[]++` возвращает 9
и сохраняет 10, а `++box[]` возвращает и сохраняет 11. Setter, возвращающий 999,
не меняет эти результаты.

### Область реализации

VM использует существующие проверки, чтение/запись свойств и индексов, `DUP`,
`DUPN`, `ROTATE` и арифметические инструкции/обновления. Новых opcode или версии
формата нет; исполняемый байткод не переписывается, путь миграции прежнего
байткода не добавляется. Продолжения оператора/getter/setter удерживают значения
при вложенных вызовах, сборке мусора, `finally` и разворачивании исключения.

Нативная компиляция сохраняет существующие доказанные числовые/строковые пути
локальных переменных. Свойства, индексы и пользовательские обработчики могут
исполняться в VM; действительная нативная интеграция операторов остаётся в
[#503](https://github.com/kniazkov/g0at/issues/503). Один лишь native-флаг не
доказывает нативный вызов обработчика. См. также [индексные обработчики](operator-index.ru.md)
и [арифметические обработчики](operator-arithmetic.ru.md).
