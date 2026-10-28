# Выбор нативного вызова / Native call dispatch

## Русский

Этап #232 подключает ABI `goat.adapter.v1` к настоящему вызову функции VM.
`src/model/module_native.h` предоставляет `bind_module_files_native`: до исполнения
хост привязывает к конкретному экземпляру модуля уже проверенный схемой и выбранный
для платформы вариант. Привязка копирует метаданные, проверяет условия и ссылки на
собственные функции/число параметров; повторная или поздняя привязка отклоняется.
Числовой `functionId` никогда не является глобальным ключом. Номер исходного файла
и адрес тела разрешаются через неизменную таблицу функций данного модуля.

Перед выбором выполняются обычные проверки VM, в том числе сохранённая ошибка
исходного файла. Для каждого вызова заново проверяются все подходящие записи:
точные скалярные типы и декларативные сравнения. Проверки не исполняют Goat-код,
не вызывают преобразования объектов, перегрузки или callback. Недостающие параметры
получают null; лишние аргументы уже вычислены, но не добавляют формальных слотов.
Нет совпадения — VM получает те же значения без повторного вычисления. Более одного
совпадения — ошибка, а не выбор первой записи. Обнаружимое пересечение отвергается
ещё при привязке. Неприменимость одного вызова не отключает следующие вызовы.

Только после единственного совпадения вызывается хостовый resolver. Он возвращает
адаптер и проверку читаемых неизменяемых диапазонов библиотеки. Через эту точку подключён
[ленивый загрузчик #210](module-loading-api.md); сам диспетчер не выполняет OS loading. В required отсутствие
применимой специализации допускает VM; ошибка загрузки выбранного варианта — нет.
В auto неудачная загрузка отключает эту библиотеку, но не чужие модули.

После входа в адаптер любой ненулевой статус, неверный тип или представление
результата является окончательной ошибкой. Даже resource-limit не разрешает
повторного исполнения на VM. Диагностика содержит модуль, функцию и нарушенное
требование. Строковый результат проверяется до чтения и копируется до освобождения
памяти вызова и аргументов. Код и ресурсы привязки живут до завершения всех потоков
и освобождения объектов. Перенаправленные clone/flat захваты и receiver-зависимые
вызовы остаются на VM: наличие специализации оригинала не доказывает её безопасность.
Вызов через исходную область файла (`lib.add(...)`) допустим: receiver совпадает с
лексическим замыканием, а не перенаправляет поиск имён. Терминальная ошибка блокирует
и последующие попытки хоста запустить тот же процесс.

Компилятор уже назначает номера всем функциям до оптимизаций и нативного отбора.
Тесты проверяют несколько файлов, вложенные и анонимные функции, одноимённые
определения и одинаковые номера при разном наборе специализаций. Неподдерживаемые
операции и недоказанные захваты не превращаются в произвольные условия выполнения.

### Проверки и границы

`python3 scripts/check_module_dispatch.py build/module_dispatch_testing` использует
реальные компиляцию, области модулей и VM; контролируемые адаптеры подключаются через
production resolver API. Сценарии 91–100 проверяются вместе с существующими
`integration.module_abi` (CTest), `check_module_consistency.py`, `check_module_interface.py`.
Новый набор включён во все Linux/ARM и Windows jobs. Подробные результаты запуска
фиксируются при проверке PR, а не предполагаются из наличия тестов.

Предварительное требование собственной совместимой специализации главного модуля,
извлечение/загрузка DLL/SO, проверка mapped ranges и фактического описания библиотеки
реализованы отдельным слоем #210. Выпуск ABI-v1 native-вариантов из `.gmod` — #211.
Общая CLI-интеграция остаётся #191; legacy query ABI не подменяет модульный ABI v1.

## English

Step #232 connects `goat.adapter.v1` to real VM function calls.
`src/model/module_native.h` exposes `bind_module_files_native`: before execution,
the host binds an already schema-validated, platform-selected variant to one exact
module instance. The binding copies metadata and validates guards, local function
references and formal arity; repeated or late binding is rejected. A numeric
`functionId` is never a global key. Source-file identity and body address resolve
through that module's immutable function table.

Ordinary VM checks, including saved declaring-file failure, precede selection.
Every call retests all relevant entries using exact scalar types and declarative
comparisons. Guards execute no Goat code, object conversion, overload or callback.
Missing parameters become null; extra arguments are already evaluated but add no
formal slots. No match falls back with the same values, without reevaluation.
Multiple matches are an error, never first-record priority. Detectable overlap is
rejected at binding. An inapplicable call does not disable subsequent calls.

Only a unique match invokes the host resolver, which returns an adapter and a
readability check for immutable library ranges. This is the integration seam used by
the [lazy loader #210](module-loading-api.md), not OS loading in the dispatcher itself. Required permits VM when no specialization
applies, but not when loading a selected variant fails. Auto disables that failed
library, not unrelated modules.

After adapter entry, every nonzero status, wrong result type or invalid value
representation is terminal. Even resource-limit does not permit a VM retry.
Diagnostics identify the module, function and violated requirement. String results
are validated before reading and copied before releasing call memory or arguments.
Binding code/resources survive all threads and managed objects. Clone/flat capture
redirection and receiver-dependent calls stay on VM: the original specialization
alone does not prove safety for relocated captures. Calls through the original
file scope (`lib.add(...)`) are eligible because the receiver is the lexical
closure, not a name-lookup redirection. A terminal failure also prevents the host
from restarting the same process.

The compiler already assigns every function an ID before optimization and native
selection. Tests cover multiple files, nested/anonymous functions, duplicate names
and stable IDs across specialization sets. Unsupported operations and unproved
captures do not become arbitrary executable predicates.

### Verification and boundaries

`python3 scripts/check_module_dispatch.py build/module_dispatch_testing` exercises
real compilation, module scopes and VM calls with controlled adapters through the
production resolver API. Decisions 91–100 are tested alongside the existing
`integration.module_abi` (CTest), `check_module_consistency.py` and `check_module_interface.py`.
The new suite runs in all Linux/ARM and Windows jobs. Actual pass results are
verified per PR, not inferred merely from having test code.

Root-module required preflight, DLL/SO extraction/loading, mapped-range validation
and actual library descriptor matching are implemented by the separate #210 layer.
Producing ABI-v1 native variants from `.gmod` is provided by #211; full CLI integration
remains #191. The legacy query ABI never substitutes for module v1.
