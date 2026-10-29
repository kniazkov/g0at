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
use `initialize_module_file_direct` on the root main file; it preserves once-only
state without the included/imported initialization mutex or dynamic restrictions.

The callback must complete synchronously, restore its nested contexts and return
true only on normal completion. The gate owns and destroys its supplied context.
The callback must propagate exceptions rather than turn them into success.
An escaping failure marks the file failed and never retries side effects. The
executor and gate both take an explicit `exception_t *failure` output: the executor
transfers one owned reference on failure; the gate returns an owned reference to
the same value on every failed entry. Both gate outputs are required and cleared
on entry; release or propagate a previous output before reusing it. A false
executor result without a payload is an invalid-operation failure; a payload
cannot be silently discarded by returning true. The Goat null singleton is a
valid exception, unlike C NULL. No implicit main-thread exception slot is used.

The file retains the exact original value as a GC root. It is neither copied nor
frozen; mutations visible through an escaped ordinary error object remain visible
on later failures. `module_file_failure`, `module_files_failure`, and
`module_scope_failure` expose borrowed values. Lookup/write/copy operations and
escaped function calls propagate this value. Function provenance survives cloning
and flattening, and the call guard runs before native selection. A scope cannot be
copied while uninitialized/initializing, so a premature copy cannot bypass a later
failure. Ordinary escaped values are not revoked. A failed prototype blocks access
when traversal reaches it, not access to a descendant's own fields. The gate now serializes included/imported initialization with the process-wide
recursive mutex described in [the concurrency API](module-serialization-api.md).

A recursive include by the initializing thread returns the same borrowed scope
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

`module_initialization_testing` and `module_initialization_scenarios` (`integration.module_initialization.*`)
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
запуска используется `initialize_module_file_direct` для главного файла корня:
однократность сохраняется, но мьютекс и динамические ограничения include/import
не включаются.

Исполнитель синхронно завершает работу, восстанавливает вложенные контексты и
возвращает true только при нормальном завершении. Переданный контекст принадлежит
машине состояний; самостоятельно освобождать его нельзя. Исключение, вышедшее из
файла, означает неудачу, а не успешное завершение. Неудача терминальна и не
повторяет побочные эффекты. Исполнитель и машина состояний принимают явный выход `exception_t *failure`:
исполнитель передаёт владение одной ссылкой на окончательно вышедшее значение,
а машина состояний возвращает принадлежащую вызывающему ссылку на тот же объект
при каждом неудачном обращении. Оба выхода обязательны и очищаются при входе;
предыдущую ссылку нужно освободить либо передать дальше до повторного использования.
False без исключения означает INVALID_OPERATION; true вместе с исключением также
не считается успехом. Goat null допустим как исключение, C NULL означает его
отсутствие. Не используется неявный слот главного потока. Сериализация через
рекурсивный мьютекс процесса описана в [API конкурентного исполнения](module-serialization-api.md).

Файл удерживает исходное значение как корень GC, не копирует и не замораживает его.
Изменения обычного объекта ошибки видны при повторном выбросе. Внутренние функции
`module_file_failure`, `module_files_failure`, `module_scope_failure` возвращают
заимствованную ссылку. Чтение, запись, копирование неудачной области и вызов ранее
переданной функции распространяют то же значение. Происхождение функции сохраняется
через clone/flat, проверка вызова предшествует выбору native. Незавершённую область
копировать нельзя, иначе копия могла бы обойти последующую неудачу. Обычные значения
не отзываются. Неудачный прототип блокирует только обращение, дошедшее до него,
а не собственные поля потомка.

Повторный вход владеющего потока при цикле include сразу возвращает ту же
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

## Saved-failure regression tests / Проверки сохранённой неудачи

`module_initialization_errors_testing` and
`module_initialization_scenarios` (`integration.module_initialization_errors.*`) verify exact payload identity,
mutable exception values, GC retention, repeated failed entry without side effects,
failed scope reads/writes/copies, escaped function calls and their copies, and
unaffected ordinary values and successfully initialized files. These internal-API
checks prepare the operator-level integration in #194/#195. The exception/finally
scenario matrix is covered below (#229).

Тесты проверяют идентичность и изменение исключения, сохранение при GC,
повторный вход без эффектов, чтение/запись/копирование неудачной области и вызовы
утёкших функций/их копий. Обычные значения и успешно завершённые файлы остаются
работоспособными. Полная интеграция операторов относится к #194/#195. Расширенная матрица
распространения исключений/finally описана ниже (#229).

## Propagation and finally (#229) / Распространение и finally

The gate records only the exception that ultimately leaves its own initializer.
A caught exception does not fail that file. An uncaught child failure is passed
unchanged through each active consumer; an already initialized dependency is not
revoked. A catch or finally that throws a new value gives the current consumer a
new failure without changing the child's saved one. Repeated failed entry does
not execute any body or cleanup again. Ordinary function failures after successful
file initialization do not change that file's state.

`exception_t` owns both its value and an optional immutable, reference-counted
origin trace. `retain_exception` retains both; `clear_exception` releases both and
clears the record. Move the complete record when transferring ownership, not only
`value`. Trace frames identify the module, source-file index and VM instruction,
from the throwing context towards its callers. `module_file_exception` and
`module_files_exception` return borrowed records; retain before storing or
propagating. Cleanup suspension preserves the same trace. A replacement exception
captures its own origin. Traces never modify the thrown object or bytecode and
are freed when the last exception record releases them; caught exceptions do not
accumulate process-lifetime trace storage.

Машина состояний запоминает только окончательное исключение, вышедшее из своего
инициализатора. Перехваченная ошибка не блокирует файл. Ошибка дочернего файла
передаётся через активных потребителей без замены, а уже готовая зависимость
остаётся готовой. Новая ошибка из catch/finally меняет ошибку текущего потребителя,
но не ранее сохранённую ошибку зависимости. Повторный вход не выполняет тело или
finally заново; ошибка обычной функции готового файла не меняет его состояние.

`exception_t` владеет значением и отдельной неизменяемой трассировкой со счётчиком
ссылок. `retain_exception` и `clear_exception` работают с обеими частями; при
передаче владения переносится вся запись, не только `value`. Кадры содержат
идентичность модуля, индекс исходного файла и инструкцию VM, начиная с места
выброса. `module_file_exception` и `module_files_exception` возвращают
заимствованную запись. Приостановка для finally сохраняет трассировку; новое
исключение получает собственную. Трассировка не изменяет объект ошибки или
байткод и освобождается после последней владеющей ссылки.

`module_exception_propagation_testing` with
`scripts/check_module_exception_propagation.py` exercises every rule 26–35 using
real VM handlers/cleanup and synchronous nested initialization callbacks. These
are internal integration tests; source include/import dispatch remains #194/#195.

The matrix has 120 independently encoded fixtures: rules 26–35 × two instruction
layouts × internal or identically named external files × object/null/integer
payloads. It checks exact origin instructions, shared and replacement traces, GC,
bytecode immutability, caller-stack preservation and repeated caught exceptions
without retained trace allocations.

Матрица содержит 120 независимо закодированных сценариев: правила 26–35, два
расположения инструкций, внутренние либо одноимённые внешние файлы и три типа
исключений (объект/null/целое). Проверяются исходные инструкции, общие и заменённые
трассировки, GC, неизменность байткода, стек вызывающего кода и отсутствие
накопления трассировок при многократном перехвате.

## No deadline and stable rethrow (#239) / Без таймаута и замены исключения

The synchronous gate has no timer, watchdog, instruction budget or elapsed-time
failure. It leaves a file in `MODULE_FILE_INITIALIZING` while its executor is
blocked in I/O and changes the state only when that executor returns. Terminating
the process from outside is not an initializer timeout and cannot be recorded as
one. This contract does not remove unrelated limits such as native cleanup-service
startup deadlines or test-harness watchdogs.

Every repeated request for a failed file returns retained references to exactly
its saved value and origin trace. A different caller, another source-file context,
GC, or mutations of the thrown object do not recapture the trace, clone the value,
or re-execute the initializer. A caller may clear its owned exception record
without changing the file's retained error. Multiple independently owned returned
records remain valid at the same time.

Синхронный механизм не содержит таймера, watchdog, лимита инструкций или ошибки по
истечении времени. При блокирующем вводе-выводе файл остаётся в состоянии
`MODULE_FILE_INITIALIZING` до возвращения исполнителя. Внешнее завершение процесса
не считается таймаутом инициализации и не записывается как её ошибка. Этот контракт
не отменяет независимых ограничений, например срока запуска уборщика нативных
файлов или защитного таймаута тестового стенда.

Повторный запрос неудачного файла возвращает владеющие ссылки на то же значение и
ту же исходную трассировку. Другой вызывающий файл или контекст, GC и изменения
объекта ошибки не создают новую трассировку, копию значения или повторный запуск.
Освобождение возвращённой записи вызывающим кодом не уничтожает сохранённую ошибку
файла; несколько полученных записей могут существовать одновременно.

`module_initialization_contract_testing` and
`scripts/check_module_initialization_contract.py` use a framed stdin stream and
an explicit READY/release handshake inside the active executor. One fixture keeps
blocking input pending beyond five seconds, then releases it; other cases have no
artificial delay. The finite observation is a regression guard against the former
proposed five-second limit, not a claim that a test can wait forever. The API and
implementation impose no deadline. Success and object/null/integer failure cases
vary instruction layout and internal/external source identity. Repeated calls
from different file/module contexts check the same value, origin trace, one-shot
side effects, GC survival and immutable bytecode. Harness watchdogs only prevent
hung tests; they do not run in the language runtime. Operator dispatch remains
part of #194/#195, which consume this shared gate.

Тест использует явные сигналы READY/release внутри активного исполнителя. Один
сценарий удерживает блокирующий ввод дольше пяти секунд и затем разрешает его;
остальные идут без искусственных задержек. Конечное наблюдение защищает от
возвращения ранее обсуждавшегося пятисекундного ограничения, но не выдаётся за
проверку бесконечного ожидания. API и реализация не устанавливают срок. Проверены
успех и исключения объект/null/целое при разных размещениях инструкций и признаках
исходного файла; повторные обращения из разных файлов и модулей сохраняют значение,
исходную трассировку, однократность эффектов и неизменность байткода после GC.
Защитные таймауты существуют только в тестовом стенде, не в runtime языка.

## Unfinished scopes (#225) / Незавершённые области

Rules 36–40 are checked by `module_incomplete_scopes_testing` and
`test/src/functional/module_incomplete_scopes_scenarios.c`. A reserved declaration is a real cell,
not a missing property: an early read or write raises a catchable uninitialized
error and does not populate, replace, or fail that cell's file by itself. After
normal declaration execution the same cell becomes available under its original
access and constness rules. Observation omits pending declarations rather than
printing null or exposing their keys; this does not make direct access succeed.
Ordinary visibility filtering still applies to ready declarations.

The checked clone/flat operation rejects the file scope until initialization
finishes. Ordinary objects created during initialization remain copyable.
Inheritance retains the actual scope as a prototype without copying its pending
cells. Early inherited access raises the same kind of error; later access sees
completed declarations and subsequent permitted mutations. The tests check both
successful and failing operations, caught errors, file states and side effects.
These are VM/model integration checks; source include/import dispatch is still
integrated separately by #194/#195.

Правила 36–40 проверяются теми же компонентными тестами. Зарезервированное
объявление — настоящая ячейка, а не отсутствующее поле: раннее чтение или запись
вызывает перехватываемую ошибку, не заполняет и не заменяет ячейку и само по себе
не переводит её файл в состояние неудачи. После выполнения объявления доступна
та же ячейка с прежними правами и константностью. Наблюдение пропускает неготовые
объявления, не подставляя null и не выдавая их ключи; прямой доступ при этом всё
ещё вызывает ошибку. Готовые объявления также фильтруются по обычным правам.

Проверяемая операция clone/flat запрещает копирование самой области до завершения
инициализации. Обычные созданные объекты остаются копируемыми. Наследование
сохраняет ссылку на область-прототип без копирования неготовых ячеек. Раннее чтение
через потомка вызывает такую же ошибку, а позднее видит готовые объявления и их
допустимые изменения. Тесты проверяют успешные и ошибочные операции, перехват,
состояния файлов и побочные эффекты. Это интеграционные проверки VM/модели;
исполнение исходных include/import подключается отдельно в #194/#195.

## Prototype failures (#226) / Ошибки прототипов

Rules 41–45 are exercised by `module_failed_prototypes_testing` and
`test/src/functional/module_failed_prototypes_scenarios.c` using independently encoded multi-file
images and the actual initialization gate. Descendant own fields remain usable
when a prototype fails, but inherited field access propagates the original error.
Shallow clone of the ordinary descendant preserves prototype references without
reading their fields. Clone of the failed file scope itself remains forbidden.
Flat validates all actual ancestors before creating a result, including deeper
and additional inheritance branches; a ready branch cannot hide a failed or
initializing branch. Failure returns an exception, never a partial object, and
leaves the source intact. Successful initialization enables the same flat operation.
Source include/import syntax is integrated separately by #194/#195.

Правила 41–45 проверяются независимо закодированными многофайловыми образами и
реальным механизмом инициализации. Собственные поля потомка доступны после ошибки
прототипа; наследуемые поля передают исходное исключение. Поверхностный clone
обычного потомка сохраняет ссылки на прототипы без чтения их полей. Копировать саму
неудачную область файла по-прежнему нельзя. Flat проверяет всех фактических предков
до создания результата, включая дальние и дополнительные ветви наследования;
готовая ветвь не скрывает неудачную или незавершённую. Ошибка не возвращает
частичный объект и не меняет исходный. После успешной инициализации тот же flat
разрешён. Исходный синтаксис include/import подключается отдельно в #194/#195.

The ordinary `child.clone()` method syntax reaches the root built-in without
reading failed prototype values when no declaration shadows `clone`. A slot of
that name in the failed scope (including an inaccessible or unfinished slot)
still shadows the built-in and propagates the failure; earlier ordinary overrides
keep their usual behavior. This narrow read fallback does not relax assignment,
ordinary inherited-field lookup, or cloning the failed scope itself.

Обычный вызов `child.clone()` находит корневую встроенную операцию без чтения
значений неудачного прототипа, если `clone` не затенён объявлением. Ячейка с таким
именем в неудачной области, в том числе недоступная или неготовая, сохраняет
затенение и передаёт ошибку; более приоритетные обычные переопределения работают
как прежде. Это узкое правило чтения не ослабляет присваивание, доступ к прочим
наследуемым полям или запрет копирования самой неудачной области.
