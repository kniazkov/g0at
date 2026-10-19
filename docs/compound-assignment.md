# Compound assignments / Составные присваивания

## English

The operators `+=`, `-=`, `*=`, `/=`, `%=`, `**=`, `&=`, `|=`, `^=`, `<<=` and `>>=`
read a target, apply the corresponding ordinary binary operation, and store its
result. Targets are variables, named properties, or indexed properties/elements.
They use the existing object operations; there are no compound-assignment object
virtual methods.

Evaluation order is part of the contract:

1. Evaluate the receiver and all indices once, from left to right, and retain them.
2. Read the old value.
3. Evaluate the right operand.
4. Apply the ordinary operation to the old value and right operand.
5. Store the result at the retained target and return the assigned value.

Thus `items[index()] += amount()` calls `index()` once. In `x += (x = 5)`,
with `x` initially 10, the result and final `x` are 15. Replacing `items` or
changing its element inside `amount()` does not change the retained receiver,
index, or old value.

These operators have the priority of `=` and associate to the right:
`a += b *= 2` means `a += (b *= 2)`. Reading an inherited property and writing
back creates/updates an own property, leaving the prototype unchanged, just as
ordinary property assignment does. Constant-write restrictions still apply.

If target evaluation, reading, the RHS, or the operation throws, no final store
is performed. Earlier side effects remain. Invalid index counts are checked by
the existing indexing methods; ordinary objects and arrays accept one index.

Native compilation uses the existing operation generator and proven local
storage. Currently `+=`, `-=` and `*=` support compatible numeric locals, and
`+=` supports string concatenation and the existing native string conversions.
As with ordinary assignment, a local must retain its native representation.
Properties, indices, captured writes, division, remainder, power and bitwise operations
retain their existing VM behavior; this feature does not introduce native
representations for objects or arrays. The native suite compares VM and native
output and verifies actual native call counts, including compiler-free execution
of saved binaries.

Bytecode format 8 adds stack-only `DUPN` and `ROTATE` instructions for retained
addresses. Recompile older `.gbin` files from source.

## Русский

Операторы `+=`, `-=`, `*=`, `/=`, `%=`, `**=`, `&=`, `|=`, `^=`, `<<=` и `>>=` —
синтаксический сахар над чтением, обычной операцией и записью. Новых виртуальных
методов объекта нет. Слева допускаются переменная, свойство через точку или
индексный доступ.

Сначала один раз вычисляются объект и индексы слева направо, затем читается старое
значение, вычисляется правая часть, выполняется операция и записывается результат.
Значение выражения — записанное значение. Например, при начальном `x = 10`
выражение `x += (x = 5)` возвращает 15 и оставляет в `x` число 15.
Изменение переменной объекта, индекса или самого элемента внутри правой части
не меняет сохранённые адрес и старое значение.

Приоритет совпадает с `=`, ассоциативность правая: `a += b *= 2` означает
`a += (b *= 2)`. Унаследованное свойство читается из прототипа, результат
записывается в собственное свойство объекта. Ограничения записи констант сохраняются.
Если вычисление адреса, чтение, правая часть или операция выбросили исключение,
итоговой записи нет; уже произошедшие побочные эффекты не откатываются.
Проверку количества и значений индексов выполняют существующие методы индексации.

Нативное исполнение использует существующий генератор операции и локальное
хранилище с доказанным неизменным нативным представлением. Сейчас это числовые
`+=`, `-=` и `*=`, а также строковое `+=` с существующими нативными преобразованиями.
Свойства, индексы, запись захваченных переменных, деление, остаток, степень и побитовые
операции сохраняют существующее исполнение в VM. Нативное представление массивов
и объектов не добавляется. Тесты сравнивают результаты VM и нативного исполнения,
проверяют счётчики нативных вызовов и запуск сохранённого бинарного файла без компилятора.

Формат байткода 8 добавляет стековые инструкции `DUPN` и `ROTATE` для сохранённых
адресов. Старые `.gbin` необходимо перекомпилировать из исходников.
