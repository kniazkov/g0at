# Function provenance and re-exports / Происхождение функций и переэкспорт (#228)

## English

Rules 1–12 in the module specification are executed through the existing source
compiler, verified graph snapshot, lazy file initialization and access policy.
A re-export is an ordinary declaration initialized with a value: it does not
create a forwarding cell, wrap a function, grant rights or copy an object.

- Dynamic functions retain their declaring module, file, lexical environment and
  explicit captures. Calls use those retained rights, including callbacks,
  escaped closures, method storage and chains of re-exports. Returning or passing
  an object does not pass the function's rights to the recipient.
- Every field is checked independently. An external object or file-scope
  reference does not make nested `public` or file-local declarations external.
  Printing also uses the executing function's rights rather than its caller's.
- Updating an existing field changes only its value, preserving the cell's
  module, file, modifier and constness. A replacement function retains its own
  declaration provenance, independently of the field storing it.
- A variable re-export creates an independent cell. Object/array/function values
  initially retain reference identity; replacing one variable does not replace
  the other. A constant reference still permits operations on a mutable value.

These semantics reuse `function_access_t`, `dynamic_call`, field cells and the
shared property-access operations; there is no separate re-export access path.
The executable regression suite is:

```sh
python3 scripts/check_module_function_provenance.py build/module_import_execution_testing
```

It compiles real multifile modules and consumer modules, packages them, loads a
verified graph and executes the immutable images. Scenarios map to all twelve
rules, checking successful access as well as denied access, reference identity,
independent variables and preserved rights after callbacks and replacement.
The shared compilation/execution harness also serves the import regression
suite. Both suites run on every Linux and Windows CI target.

This establishes runtime behavior, not the exported analytical interfaces and
`functionRef` metadata of #205, native-call integration of #232, or the final
public CLI integration of #191.

## Русский

Правила 1–12 спецификации модулей исполняются через существующие компилятор
исходников, проверенный снимок графа, ленивую инициализацию файлов и политику
доступа. Переэкспорт — обычное объявление со значением: он не создаёт связанную
ячейку, не оборачивает функцию, не предоставляет права и не копирует объект.

- Динамическая функция сохраняет объявляющие модуль и файл, лексическое окружение
  и явные захваты. Вызов использует эти права, включая callbacks, вышедшие из
  области замыкания, хранение как метода и цепочки переэкспорта. Возврат или
  передача объекта не передаёт получателю права функции.
- Каждое поле проверяется отдельно. Внешняя ссылка на объект или область файла
  не превращает вложенные `public` и файловые объявления в `external`.
  Печать также использует права исполняемой функции, а не вызывающего кода.
- Присваивание существующему полю изменяет только значение, сохраняя модуль,
  файл, модификатор и константность ячейки. Подставленная функция сохраняет
  собственное происхождение независимо от хранящего её поля.
- Переэкспорт переменной создаёт независимую ячейку. Объекты, массивы и функции
  первоначально сохраняют идентичность ссылок; замена одной переменной не меняет
  другую. Константная ссылка допускает операции над изменяемым значением.

Используются `function_access_t`, `dynamic_call`, ячейки полей и общие операции
доступа; отдельного пути проверки переэкспорта нет. Запуск сквозных проверок:

```sh
python3 scripts/check_module_function_provenance.py build/module_import_execution_testing
```

Набор компилирует настоящие многофайловые библиотеки и модули-потребители,
упаковывает их, загружает проверенный граф и исполняет неизменяемые образы.
Сценарии соответствуют всем двенадцати правилам и проверяют разрешённый и
запрещённый доступ, идентичность ссылок, независимость переменных, сохранение
прав после callbacks и замены функций. Общий тестовый помощник компиляции и
исполнения используется также регрессионным набором import. Оба набора
выполняются на каждой Linux/Windows-платформе CI.

Это исполняемая семантика, а не экспорт аналитических интерфейсов и метаданных
`functionRef` из #205, интеграция нативных вызовов #232 или окончательная
пользовательская CLI-интеграция #191.
