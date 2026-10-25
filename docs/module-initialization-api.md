# Lazy file initialization (#196)

`model/module_files.h` owns the single initialization state for each persistent
file scope: `UNINITIALIZED`, `INITIALIZING`, `INITIALIZED`, or `FAILED`. This state
is separate from immutable bytecode. Snapshot preparation creates scopes but
never runs their code. An unexecuted include/import branch leaves its target
uninitialized.

`initialize_module_file` is the synchronous gate used by future include/import
executors. It validates the file, caller, callback and output before changing
state. First entry reserves the file's top-level declarations, marks the file
initializing, creates an isolated file context, and invokes the supplied executor.
The executor receives the actual file and uses `module_file_entry` in its one
shared bytecode image; `main` selects a file, not instruction zero. For import,
pass `module_files_main` of the already preflighted dependency. For direct launch,
pass the root main file; the caller remains responsible for the different dynamic
execution mode specified for `exit` (#207).

The callback must complete synchronously, restore its nested contexts and return
true only on normal completion. The gate owns and destroys its supplied context.
The callback must propagate exceptions rather than turn them into success.
An escaping failure marks the file failed and never retries side effects; storing
and rethrowing the original payload is the next task #197. This API is not an OS
thread lock; the scheduler's serialization policy is #217.

A recursive include of an initializing file returns the same borrowed scope
without calling the executor. Completed files likewise return the same current
scope and mutable state. Files and module versions have independent state.
Import cycles still fail whole-graph preflight before any runtime is prepared;
this gate does not weaken that check or perform filesystem lookups.

## Declarations and diagnostics

Top-level `DVAR`/`DCONST` declarations are reserved before code starts, including
forward declarations reached after a cyclic include. Reservations preserve their
binding IDs, access modifiers, constants and declaring-file identity. Nested
function/block declarations are not promoted to file scope. Executing the
matching declaration initializes the existing cell rather than replacing it.

Reading or writing a reserved, not-yet-initialized cell returns
`MSTAT_UNINITIALIZED`, not a missing property or an ordinary null value. The
model exposes `Exceptions.UNINITIALIZED_DECLARATION`; VM exceptions add the active
initialization chain to its textual diagnostic. Access
checks still apply. An early read alone does not fail its owning file: the caller
may catch the error and allow initialization to finish. Observation omits pending
cells. `module_initialization_chain` captures the active root-to-leaf chain while
an executor is running; names are escaped and file indices/external markers are
included. The chain is restored on normal and failed unwinding.

`module_initialization_testing` and `scripts/check_module_initialization.py`
exercise the state gate and actual VM declarations with independently encoded
images. Language operator dispatch and complete CLI routing remain #194/#195 and
#191; this task supplies their shared state machine, not another parser.

# Ленивая инициализация файлов (#196)

`model/module_files.h` хранит единое состояние каждой постоянной области файла:
`UNINITIALIZED`, `INITIALIZING`, `INITIALIZED` или `FAILED`. Состояние находится
вне неизменяемого байткода. Подготовка снимка создаёт области, но не выполняет их
код; невыполненная ветка include/import не запускает инициализацию зависимости.

`initialize_module_file` проверяет аргументы до изменения состояния. При первом
входе резервируются верхнеуровневые объявления, создаётся изолированный контекст
файла и вызывается переданный исполнитель. Он использует `module_file_entry` в
общем байткоде. `main` выбирает логический файл, а не нулевую инструкцию; import
использует `module_files_main` заранее проверенной зависимости. Для прямого
запуска выбирается главный файл корня. Различие динамического режима прямого
запуска и импорта для `exit` сохраняется и подключается в #207.

Исполнитель синхронно завершает работу, восстанавливает вложенные контексты и
возвращает true только при нормальном завершении. Переданный контекст принадлежит
машине состояний; самостоятельно освобождать его нельзя. Исключение, вышедшее из
файла, означает неудачу, а не успешное завершение. Неудача терминальна и не
повторяет побочные эффекты. Хранение исходного исключения и повторный выброс
добавляются в #197; сериализация потоков относится к #217.

Повторный вход в уже выполняющийся файл при цикле include сразу возвращает ту же
область без повторного вызова исполнителя. После успешного завершения возвращается
та же область с текущими значениями. Файлы и версии модулей имеют независимые
состояния. Циклический import по-прежнему отклоняется до исполнения общим preflight.
Машина состояний не читает файлы с диска и не изменяет образ байткода.

Верхнеуровневые `DVAR`/`DCONST` резервируют ячейки до выполнения кода, сохраняя
идентичность, права, константность и файл объявления. Объявления функций и вложенных
блоков не переносятся в область файла. Само объявление заполняет прежнюю ячейку.
Раннее чтение или запись возвращает `MSTAT_UNINITIALIZED`, а не отсутствующее поле
или null; права доступа проверяются отдельно. Ранняя ошибка сама по себе не
переводит файл в состояние неудачи, её можно перехватить. Наблюдение пропускает
неготовые ячейки. Модель предоставляет `Exceptions.UNINITIALIZED_DECLARATION`;
VM дополняет текст ошибки активной цепочкой инициализации. `module_initialization_chain` сохраняет текущую цепочку для
диагностики с экранированными именами, индексами и признаками внешних файлов;
после выхода активная цепочка восстанавливается.

Компонентные тесты проверяют машину состояний и настоящее исполнение VM на
независимо закодированных образах. Синтаксическое исполнение include/import и
полная CLI-интеграция остаются задачами #194/#195 и #191.
