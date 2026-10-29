# Concurrent initialization and program threads (#217)

## Runtime gates and lock order

`initialize_module_file` remains the include/import gate. It serializes first
execution across all files of a process with one recursive initialization mutex.
The owner may recursively include a file already on its initialization chain;
other threads wait. Terminal success returns the same persistent scope. Terminal
failure returns the same retained exception value and immutable original trace,
without rerunning side effects. Import cycles still fail preflight.

`initialize_module_file_direct` is the direct-entry gate. It preserves once-only
file state and declaration reservation but does not acquire the initialization
mutex. Other threads can call ready entry-file functions and read/write ready
cells subject to access checks. Pending declarations throw
`UNINITIALIZED_DECLARATION` immediately rather than waiting for entry completion.
Module gates reserve declarations eagerly using the shared
`prepare_file_scope_declarations` walker. Standalone `.goat`/standalone `.gbin`
execution instead calls `prepare_remaining_file_scope_declarations` immediately
before the first actual spawn, under the execution mutex and before starting its
OS thread. This reserves still-unexecuted entry declarations while keeping existing
initialized cells. Before a worker exists, ordinary same-thread lookup preserves
uses of a builtin before a later declaration shadows it. Neither path mutates
bytecode.
This distinction applies to entry execution, not to all functions declared there:
an entry function called inside an include/import initializer inherits that
caller's dynamic restrictions.

A second, recursive process execution mutex protects model operations and state
publication. `process_execution_enter`/`process_execution_leave` acquire/release
one level. `process_execution_suspend` releases **all** currently owned levels and
returns their count; `process_execution_resume` restores that exact count. Hosts
using the callback gate must use these around blocking host operations that need
other VM threads to progress. They must not access shared model objects while the
execution mutex is suspended. This does not release the initialization mutex.

Loader waits release the execution mutex before waiting for initialization, then
restore it. This ordering prevents a waiter from blocking the initializer's next
model operation. VM instruction boundaries, program joins and supported blocking
I/O allow ordinary threads to progress while an initializer retains its distinct
initialization mutex. Calls to ready independent functions do not acquire that
mutex. Blocking I/O is allowed; no initialization timeout is introduced.

The portable `lib/os_thread.h` layer provides `create_os_recursive_mutex` using the
same lock/try-lock/unlock API as nonrecursive mutexes. `os_mutex_depth` returns the
calling OS thread's acquisition depth or zero. A condition wait fully releases a
recursive mutex and restores its previous depth after waking. These are internal
primitives, not new user-language mutex objects.

## Language operations and ownership

- `spawn(callable, ...arguments)` starts an OS-backed execution in the existing
  `thread_t` model and returns a stable positive integer ID, looked up within the
  current process. Callable and arguments remain retained until execution uses
  them; no new main/secondary thread hierarchy is introduced.
- `join(id)` returns the completed invocation's result or propagates its exact
  retained exception and original trace. Repeated joins are valid. Any registered
  program thread ID is accepted, including the entry execution; IDs are visible
  to the host, but there is no language builtin exposing the entry ID. Unknown IDs
  are `INVALID_ARGUMENT`; self-join and cyclic joins are `INVALID_OPERATION`.
- Both operations are `INVALID_OPERATION` while the caller's
  `initialization_depth` is nonzero. The restriction covers helper functions from
  every file and native-to-VM paths; it does not affect other ordinary threads.
  Nested initialization restores its previous depth on all exits. Repeated entry
  to an already ready file does not create a new restricted context.
- A joined worker exception is observed by the joining program and may be caught.
  An unobserved worker exception makes ordinary execution return a failure status.

`run` and `run_module_file` retain shared caches, file scopes, snapshots and native
infrastructure until all program threads finish. A surviving thread may spawn a
new thread, use the ready entry-file declarations, import a module or first invoke
a native specialization after the entry execution has finished. Completion is
tracked across that changing population. OS handles are joined and ordinary
cache/GC finalization happens only after the last program execution finishes.

In concurrency-capable compilation units, analysis conservatively treats shared
mutable facts and closed scalar transformations: another thread may change a
value between instructions, so an earlier observation is not a license to fold
later shared reads. Known pure spawn callbacks may still receive applicable native
specializations.

Synchronization publishes readiness and values safely; it does not turn arbitrary
user read-modify-write sequences into atomic operations. The full bounded explicit
`exit`/external-cleanup protocol is implemented separately by #230
([API](module-exit-api.md)); it does not wait for blocked workers.

# Конкурентная инициализация и потоки программы (#217)

`initialize_module_file` сериализует инициализаторы всех файлов процесса одним
рекурсивным мьютексом. Повторный вход владельца при циклическом include возвращает
частичную область; остальные потоки ожидают завершения. Успех возвращает ту же
постоянную область, ошибка — то же значение исключения с исходной трассировкой.
Повторного исполнения нет; циклы import отклоняются до запуска.

`initialize_module_file_direct` сохраняет однократность и резервирование объявлений
прямого входного файла, но не захватывает мьютекс инициализации. Готовые функции и
ячейки доступны другим потокам с обычной проверкой прав. Неготовое объявление
вызывает `UNINITIALIZED_DECLARATION` без ожидания всего файла. Входы модульных
файлов резервируют объявления заранее общим обходом
`prepare_file_scope_declarations`. Standalone `.goat`/`.gbin` использует
`prepare_remaining_file_scope_declarations` непосредственно перед первым фактическим
spawn, под мьютексом исполнения и до запуска потока ОС. Резервируются ещё не
выполненные объявления входного файла, а готовые ячейки сохраняются. Пока рабочих
потоков нет, обычное однопоточное разрешение имён сохраняет возможность вызвать
встроенную функцию до более позднего объявления, затеняющего её имя. Байткод не
изменяется. Функция входного файла, вызванная из include/import, всё же наследует
динамические ограничения вызывающего инициализатора.

Отдельный рекурсивный мьютекс исполнения защищает модель и публикацию состояния.
`process_execution_enter`/`process_execution_leave` меняют глубину захвата на один.
`process_execution_suspend` освобождает все принадлежащие вызывающему уровни и
возвращает их число; `process_execution_resume` восстанавливает именно это число.
Исполнители C-callback должны применять их вокруг блокирующих операций хоста,
которым нужен прогресс других потоков VM, и не обращаться к общей модели без
мьютекса. Мьютекс инициализации при этом остаётся захваченным.

Внутреннее ожидание загрузчика освобождает мьютекс исполнения перед ожиданием
инициализации и восстанавливает его после; иначе ожидающий заблокировал бы
следующую операцию инициализатора. Границы инструкций VM, join и поддерживаемый
блокирующий I/O дают обычным потокам возможность продолжить работу. Готовая
независимая функция не захватывает мьютекс инициализации. I/O разрешён; таймаута
инициализации в языке нет.

`create_os_recursive_mutex` расширяет существующий API `lib/os_thread.h`;
`os_mutex_depth` возвращает глубину захвата текущим потоком ОС либо ноль.
Ожидание condition полностью освобождает рекурсивный мьютекс и восстанавливает
его глубину после пробуждения. Пользовательских объектов мьютекса это не вводит.

`spawn(callable, ...arguments)` запускает поток ОС на основе существующего
`thread_t`, удерживает callable и аргументы и возвращает стабильный положительный
целочисленный ID с поиском в текущем процессе. `join(id)` возвращает результат
вызова либо исходное исключение с его трассировкой; повторный join допустим.
Допустим ID любого зарегистрированного потока программы, включая входной поток.
ID видны хосту, но встроенной функции языка для получения ID входного потока нет.
Неизвестный ID даёт `INVALID_ARGUMENT`; ожидание себя и циклическое ожидание дают
`INVALID_OPERATION`.

Обе операции вызывают `INVALID_OPERATION` при ненулевом `initialization_depth`
вызывающего потока, включая вызовы через функции любого файла и переходы native/VM.
Другие обычные потоки ограничение не затрагивает. Вложенный инициализатор
восстанавливает прежнюю глубину при любом выходе; готовый include/import не создаёт
новый контекст. Исключение, полученное через join, считается наблюдавшимся и может
быть перехвачено. Ненаблюдавшееся исключение рабочего потока означает неудачное
обычное завершение программы.

`run` и `run_module_file` удерживают общие кеши, области, снимки и инфраструктуру
native до завершения всех потоков. Оставшийся поток может создать новый, использовать
готовые объявления входного файла, импортировать модуль и впервые вызвать нативную
специализацию после завершения входного исполнения. Условие завершения учитывает
изменяющееся множество потоков; только после последнего завершаются OS handles и
выполняется обычная финализация кешей/GC.

В единицах компиляции с возможной конкурентностью анализ консервативно обращается
с общими изменяемыми фактами и преобразованиями замкнутых скалярных вычислений:
между инструкциями значение может изменить другой поток, поэтому прежнее
наблюдение не позволяет сворачивать последующие общие чтения. Известные чистые
callback-функции spawn по-прежнему могут получать применимые нативные специализации.

Синхронизация публикует готовность и значения, но не делает произвольные составные
операции пользователя атомарными. Полный ограниченный по времени протокол явного
`exit` и внешней очистки реализован в #230 ([API](module-exit-api.md));
он не ждёт завершения заблокированных потоков.


## Acceptance coverage / Приёмочные проверки

`module_serialization_scenarios` exercises controlled concurrent requests,
serialized independent initializers, saved failures and reentrant includes.
`scripts/check_module_threads.py` exercises the language/VM/native thread paths,
dynamic restrictions, direct-entry readiness and blocking-I/O handshakes.
`module_thread_lifetime_testing` checks entry → worker → grandchild survival,
first native use after entry completion and exactly-once final cleanup. Tests use
explicit synchronization rather than random delays. Host-side deadlines are hang
protection, not a language initialization timeout. The process diagnostic
`initialization_waiters` is inspected only while holding the execution mutex.

`module_serialization_scenarios` проверяет управляемые конкурентные запросы,
сериализацию независимых инициализаторов, сохранённые ошибки и повторный include.
`scripts/check_module_threads.py` проверяет потоки через язык/VM/native, динамические
ограничения, готовность прямого входного файла и согласованное блокирующее I/O.
`module_thread_lifetime_testing` проверяет цепочку входной поток → рабочий → потомок,
первый вызов native после завершения входного потока и единственную финальную
очистку. Порядок задаётся явной синхронизацией, а не случайными задержками; лимит
стенда защищает от зависания, не вводя таймаут языка. Диагностический счётчик процесса
`initialization_waiters` читается только под мьютексом исполнения.

Run the migrated serialization cases with
`ctest --test-dir build --output-on-failure -R "^integration.module_serialization\\."`
(or select a stable ID directly; see [scenario map](../test/src/functional/README.md)).
