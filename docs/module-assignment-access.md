# Assignment access and evaluation order / Права и порядок присваиваний (#227)

## English

Rules 13–25 use the existing field cells and access policy, for both named and
computed properties. Assignment prepares its destination before evaluating the
right-hand side (RHS): receiver first, then indices in source order, each once.
A non-mutating preflight checks writability. Compound assignment additionally
reads the old value once before RHS and retains that value for the operation.
Denied access, a constant cell, or forbidden foreign-object extension therefore
prevents RHS effects. Receiver/index effects already performed are not undone.

Preflight does not reserve or create a field. After RHS, the ordinary store checks
the retained destination again against its current state. A newly inaccessible
field causes an error; an accessible field is updated in place, preserving its
ownership and access metadata. An RHS exception performs no final store, without
rolling back RHS effects. Rebinding the receiver or key variable does not redirect
the store. An array index is a retained position, not an element identity: removing
an earlier element changes which element occupies that position; clearing the
array makes the final store invalid and does not grow the array.

`CHECKPROP`, `CHECKINDEX`, `CHECKVAR` and `CHECKBOUND` are non-mutating VM write
checks. Final stores continue through the same access policy rather than trusting
the earlier result. Source analysis visits assignment address operands before RHS,
matching execution order and preventing optimizations from reversing effects.

Run the mapped executable scenarios with:

```sh
python3 scripts/check_module_assignment_access.py build/module_import_execution_testing build/goat
```

The suite covers each rule independently, both dot and computed access where
applicable, exact receiver/key/RHS effects, final values and caught errors. It
compiles real multifile and cross-module programs into verified immutable images;
standalone cases also check optimized execution. Linux and Windows CI execute the
same suite. This does not implement the separate analytical export contract #205,
native-call guards #232, or public module CLI #191.

## Русский

Правила 13–25 используют существующие ячейки полей и политику доступа как для
именованных, так и для вычисляемых свойств. Присваивание готовит цель до вычисления
правой части (RHS): сначала объект, затем индексы в исходном порядке, каждый ровно
один раз. Предварительная проверка права записи не меняет состояние. Составное
присваивание дополнительно читает старое значение один раз до RHS и сохраняет его
для операции. Запрет доступа, константная ячейка или запрещённое расширение чужого
объекта исключают эффекты RHS. Уже выполненные эффекты объекта/индекса не откатываются.

Предварительная проверка не резервирует и не создаёт поле. После RHS обычная запись
повторно проверяет сохранённую цель с учётом её текущего состояния. Новое недоступное
поле вызывает ошибку; доступное поле обновляется на месте с сохранением владельца
и метаданных доступа. Исключение RHS отменяет финальную запись, но не откатывает
эффекты RHS. Перепривязка переменной объекта или ключа не перенаправляет запись.
Индекс массива сохраняет позицию, а не идентичность элемента: удаление более раннего
элемента меняет содержимое позиции; очистка массива делает финальную запись
недопустимой и не увеличивает массив.

`CHECKPROP`, `CHECKINDEX`, `CHECKVAR` и `CHECKBOUND` — проверки права записи VM без
изменения состояния. Финальные записи проходят ту же политику доступа, не доверяя
предварительному результату. Анализ исходников посещает адресные операнды до RHS,
следуя порядку исполнения и не позволяя оптимизациям переставлять эффекты.

Запуск сквозных сценариев с привязкой к правилам:

```sh
python3 scripts/check_module_assignment_access.py build/module_import_execution_testing build/goat
```

Набор проверяет каждое правило отдельно, точечный и вычисляемый доступ, где применимо,
точные эффекты объекта/ключа/RHS, итоговые значения и перехваченные ошибки. Настоящие
многофайловые и межмодульные программы компилируются в проверенные неизменяемые образы;
отдельные сценарии также проверяют оптимизированное исполнение. Linux и Windows CI
запускают один набор. Это не реализует отдельные задачи аналитического экспорта #205,
проверки нативных вызовов #232 или публичной модульной CLI #191.
