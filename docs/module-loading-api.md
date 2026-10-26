# Ленивая нативная загрузка / Lazy native loading

## Русский

Этап #210 соединяет проверенный снимок графа `module_snapshot_t` с диспетчеризацией
модульного ABI v1. API `src/vm/module_loading.h` подготавливает режим выполнения
до пользовательского кода. Он не заменяет `load_module_snapshot`: весь граф,
структура и хеши **всех** нативных частей, включая чужие платформы и режим `off`,
сначала проверяются общим валидатором. Повреждение не разрешает переход на VM.
Байткод и архивные байты остаются неизменными; состояния загрузки хранятся отдельно.

`prepare_module_snapshot_native(snapshot, options)` вызывается ровно один раз до
исполнения. `module_loading_options_t` задаёт режим, общий заимствованный уборщик
(при NULL создаётся собственный контекст), необязательный профиль среды и callback
диагностики. `module_loading_result_t` содержит статус, номер узла и принадлежащий
результату текст; `clear_module_loading_result` освобождает текст. Ошибка проверки
платформы, required или привязки запрещает исполнение этого снимка. Ошибочный
повторный/поздний вызов API отклоняется, не изменяя уже выполняющийся процесс. Callback и его состояние, а также переданный
уборщик должны пережить исполнение снимка. Callback синхронный, не вызывает Goat
и не входит повторно в API; без callback предупреждения идут в stderr.
`module_snapshot_native_statistics` наблюдает попытки и состояние без подготовки,
`module_snapshot_native_directory` возвращает известный путь либо NULL. Путь
может сохраниться и после отказа подготовки; его наличие не доказывает READY.
Необязательный writer предназначен для интеграции/проверок отказов I/O: он не
закрывает поток; завершение записи и проверку файла выполняет загрузчик.

В `off` не выбирают и не извлекают библиотеки, не запускают уборщик и OS loader.
В `auto` отсутствие совместимого варианта означает VM; неоднозначный выбор —
ошибка подготовки, а не приоритет первого варианта. В `required` собственный
главный модуль обязан иметь хотя бы одну специализацию в совместимом варианте.
Специализации зависимостей не засчитываются. Эта проверка только по метаданным:
она не открывает библиотеку и не требует фактического нативного вызова. Зависимости
могут быть чисто байткодовыми или иметь только несовместимые варианты.

Применимость заново проверяется для каждого вызова общим диспетчером #232.
Аргументы вычислены ровно один раз; отсутствие подходящей специализации допускает
VM даже в `required`. Простой import, успешный выбор платформы или неприменимый
вызов не извлекают библиотеку. Только первый применимый вызов подготавливает
выбранную библиотеку; последующие вызовы используют тот же результат. Библиотека
принадлежит конкретному экземпляру модуля, а не глобальному числовому functionId.
Другие физические копии архива не ищутся и их варианты не объединяются.

До записи файлов необходим READY общего `cleanup_service_t` (#245), разделяемого
с временной компиляцией. Сервис имеет одну попытку и общий монотонный срок 1 с;
конкурентные вызовы не создают новых уборщиков и не продлевают срок. После отказа
готовности `auto` один раз сообщает причину и отключает native для всего запуска;
`required` получает окончательную ошибку. До READY нет извлечённых DLL/SO и загрузки
ОС. Уже выполненные эффекты программы не откатываются.

Извлечение использует удерживаемые в памяти байты проверенного снимка: удаление
или замена исходного `.gbin` после проверки не влияет на библиотеку. Файлы находятся
в собственном временном каталоге запуска; постоянного кеша нет. Частично записанный
файл никогда не передаётся загрузчику. Ошибка извлечения, OS loader или проверки
загруженного ABI в `auto` даёт одно сообщение и VM для этой библиотеки до конца
запуска; остальные библиотеки остаются доступны. В `required` такая ошибка выбранной
применимой библиотеки окончательна, включая библиотеку зависимости. Отказ сохраняется,
повторных извлечений, загрузок и предупреждений нет.

Загрузчик проверяет читаемые диапазоны отображённой библиотеки до чтения её
ограниченного дескриптора. Описание сравнивается с метаданными архива; **все**
объявленные адаптеры должны существовать и принадлежать допустимым исполняемым
диапазонам. Отсутствие даже не вызываемой специализации делает библиотеку непригодной.
Для строковых результатов проверяется принадлежность читаемому диапазону;
результат копируется до освобождения памяти вызова. Нативный код остаётся доверенным:
OS loading может исполнить конструкторы, а проверка ABI не является песочницей.
После входа в адаптер ошибка ABI никогда не разрешает повторное исполнение на VM.

На Linux экспорт проверяется не только по адресу в RX-сегменте: именованный
символ обязан иметь тип `STT_FUNC` и принадлежать той же загруженной библиотеке.
Для musl, где нет `dladdr1`, ограниченный разбор отображённых динамических
ELF-таблиц GNU/SYSV устанавливает имя, тип и адрес без повторного чтения файла.
Проверки диапазонов, принадлежности и неизменности снимка не ослабляются; данные,
оказавшиеся в исполняемом сегменте, не становятся допустимой функцией.

Нативный вход сохраняет требование свободного стека не менее 512 КиБ. На musl
`pthread_getattr_np` для главного потока может сообщать только уже отображённые
страницы. Дополнительная проверка учитывает расширение Linux `[stack]` только
для исходного главного потока: нужен конечный `RLIMIT_STACK`, точное имя `[stack]`,
права `rw-p` и отметка роста вниз `VmFlags: gd` в `/proc/self/smaps`.
Свободный промежуток до предыдущего отображения учитывает 256 страниц реального
системного размера либо переопределение `stack_guard_gap=` из `/proc/cmdline`.
Недоступные, слишком большие или некорректные данные политики `/proc` не разрешают
native. Фиксированные
pthread-стеки так не расширяются. Создаваемые Goat Linux-потоки с размером стека
по умолчанию получают не менее 8 МиБ; явно заданный малый размер не увеличивается.

Загруженные библиотеки живут до завершения всех потоков и использования объектов.
Освобождение снимка не выполняет синхронную уборку каталога. Уборщик ждёт фактической
смерти процесса-владельца, затем удаляет собственные файлы и завершает работу;
его ошибка не меняет код завершения Goat. Если READY уже подтверждён, последующая
гибель уборщика не отключает выполняющийся код и не разрешает компенсирующее
удаление или выгрузку из-под него. Очистка после такого отказа не гарантируется.

### Проверки и границы

`check_module_loading.py` использует production API, реальные библиотеки и
изолированные процессы для проверки режимов, отложенной загрузки, ошибок ABI,
снимка, конкурирующих вызовов и жизненного цикла уборщика. Существующие наборы
проверяют контейнеры/графы, ABI, per-call dispatch, сборку и сервис очистки.
Наличие теста не означает успешный запуск: результаты проверяются на каждом PR.

CLI `.gmod`/`.gbin` подключён в #191 ([команды](module-cli-api.md)); завершение
с ограниченными сроками и сбросом вывода реализовано в #230 ([API](module-exit-api.md)); проба `goat-inspect` — #214.
Временная прямая `.goat` компиляция уже использует тот же сервис по #211.
В объединённом запуске build→execution владелец CLI #191 согласует политику
одного сообщения между потребителями общего сервиса; один экземпляр уборщика
сам по себе не объединяет пользовательские callback разных контекстов.

## English

Step #210 connects the validated `module_snapshot_t` graph to module ABI v1
call dispatch. `src/vm/module_loading.h` prepares execution mode before user code.
It does not replace `load_module_snapshot`: the common validator first checks the
whole graph, structure and hashes of **all** native payloads, including foreign
platforms and `off` mode. Corruption never permits VM fallback. Bytecode and
archive bytes remain immutable; loading state is stored separately.

Call `prepare_module_snapshot_native(snapshot, options)` exactly once before
execution. `module_loading_options_t` supplies mode, a borrowed shared cleaner
(NULL creates an owned context), optional environment facts and a diagnostic
callback. `module_loading_result_t` contains status, node and owned diagnostic
text; `clear_module_loading_result` releases that text. Platform, required or
binding preflight failure forbids execution of this snapshot. Invalid repeated
or late API calls are rejected without changing an already executing process. Callback state and a supplied cleaner must
outlive snapshot execution. Callbacks are synchronous, run no Goat code and must
not reenter the API; absent a callback, warnings go to stderr.
`module_snapshot_native_statistics` observes attempts/state without preparation;
`module_snapshot_native_directory` returns a known path or NULL. A path may
survive failed startup and does not by itself prove READY. The optional
writer supports I/O integration/fault checks and must not close its stream;
the loader completes the write and verifies the file before loading.

`off` performs no library selection/extraction, cleaner startup or OS loading.
In `auto`, no compatible variant means VM; ambiguous selection is a preparation
error, not first-record priority. `required` demands at least one specialization
owned by the root module in a compatible variant. Dependency specializations do
not count. This metadata-only preflight opens no library and does not require
that a native call actually occurs. Dependencies may be bytecode-only or contain
only incompatible variants.

The shared #232 dispatcher retests applicability for every call. Arguments are
evaluated exactly once; no matching specialization permits VM even in `required`.
Import alone, platform compatibility and inapplicable calls do not extract code.
Only the first applicable call prepares the selected library; later calls reuse
that result. A library belongs to one exact module instance, never a global numeric
functionId. No alternative physical archive is sought or automatically combined.

The shared `cleanup_service_t` (#245), also used by temporary compilation, must
confirm READY before file writes. One attempt has one monotonic 1 s deadline;
concurrent callers neither start additional cleaners nor extend the deadline.
Readiness failure produces one warning and run-wide native disablement in `auto`,
or a terminal error in `required`. No extracted DLL/SO or OS loading precedes READY.
Previously executed program effects are not rolled back.

Extraction uses retained validated snapshot bytes: deleting or replacing the
source `.gbin` after validation does not affect the library. Files belong to the
run's private temporary directory; there is no persistent cache. A partially
written file never reaches the OS loader. Extraction, OS-loader or loaded-ABI
validation failure in `auto` emits one diagnostic and permanently falls back for
that library, without disabling other libraries. In `required`, failure of a
selected applicable library is terminal, including dependencies. Failures are
sticky: no repeated extraction, load attempts or warnings.

The loader checks readable mapped library ranges before reading its bounded
descriptor. The description must match archive metadata, and **every** declared
adapter must exist in an accepted executable range. Even a missing specialization
that was never called invalidates the library. String results must belong to a
readable range and are copied before call memory is released. Native code remains
trusted: OS loading can execute constructors; ABI checks are not a sandbox.
After adapter entry, an ABI failure never permits execution again on the VM.

On Linux, an address in an RX segment is not enough: the named export must be
an `STT_FUNC` belonging to the same loaded library. For musl, which lacks
`dladdr1`, bounded inspection of mapped GNU/SYSV dynamic ELF tables establishes
the symbol name, type and address without rereading the file. Range, ownership
and immutable-snapshot checks are not weakened; data sharing an executable
segment does not become a callable function.

Native entry retains the requirement for at least 512 KiB of stack headroom.
On musl, `pthread_getattr_np` may report only the main thread's already-mapped
pages. An additional check accounts for Linux `[stack]` growth only in the
original main thread: it requires a finite `RLIMIT_STACK`, the exact `[stack]`
mapping with `rw-p` permissions and the grow-down `gd` VmFlag in `/proc/self/smaps`.
The unmapped gap above the preceding mapping accounts for 256 actual system
pages or the `stack_guard_gap=` override from `/proc/cmdline`. Missing, unreadable,
oversized or malformed `/proc` policy information never grants native entry. Fixed
pthread stacks are never enlarged by this check. Goat-created Linux threads
using the default stack size receive at least 8 MiB; explicitly requested small
stack sizes are not increased.

Loaded libraries survive all program threads and managed-object uses. Freeing
the snapshot does not synchronously clean its directory. The cleaner waits for
actual owner-process death, removes its files and exits; cleanup failure does not
change Goat's exit code. Once READY was confirmed, later cleaner death neither
stops running code nor permits compensating deletion or unloading underneath it.
Cleanup after that failure is not guaranteed.

### Verification and boundaries

`check_module_loading.py` uses production APIs, real libraries and isolated
processes to exercise modes, deferred loading, ABI failures, snapshots,
concurrent calls and cleaner lifetime. Existing suites cover containers/graphs,
ABI, per-call dispatch, native building and the cleanup service. Test presence
is not a pass claim: actual results are checked for each PR.

The `.gmod`/`.gbin` CLI is integrated in #191 ([commands](module-cli-api.md));
bounded shutdown and output flushing are implemented in #230 ([API](module-exit-api.md)); isolated `goat-inspect` loading remains #214. Temporary
direct `.goat` compilation already shares this cleanup service through #211.
The combined build→execution CLI owner (#191) coordinates one-warning policy
across service consumers; sharing a cleaner does not merge
user callbacks belonging to distinct contexts.
