# In-memory launch snapshot (#192)

`vm/module_snapshot.h` prepares an owned runtime snapshot on top of the shared
whole-graph validator. `load_module_snapshot` first reads and validates the root
and every reachable transitive archive, using the single supplied search-path
snapshot. Only `MODULE_GRAPH_OK` permits creation of a process and its persistent
file scopes. Missing, corrupt, cyclic or hash-conflicting dependencies return no
runnable snapshot, including dependencies in unreachable source branches.

An optional diagnostics output owns the invalid graph; without that output it is
freed automatically. Argument/memory failure clears both outputs. The core allocator's fatal-OOM policy is unchanged: it reports exhaustion or
size overflow and exits with failure. Recoverable allocation failures in the
shared JSON/ZIP validator return `MODULE_GRAPH_MEMORY`, not a corruption error.
Loading stops immediately without scanning another copy or preparing user code.
See [resource-failure checks](module-memory-api.md) for #236. No partially prepared runtime is
published. Validation callbacks run only during preflight and must not execute
user code or mutate inputs.

## Ownership and immutable images

The snapshot owns its process and the validated graph. The graph retains all
expanded archive contents, including metadata and native payloads, irrespective
of whether native execution will be used. Each runtime module has an independent
bytecode wrapper borrowing the validated image's const views. There is no third
copy of the bytecode and no modification of instructions, strings or tables.
Native bindings and declaration identities remain in the external wrapper; file
scopes and initialization state are also outside the image.

All accessors return borrowed views. Release search paths/options after loading;
they are not needed later. Do not independently destroy the returned process,
files or validated modules. After all execution, threads and callbacks have
stopped, `free_module_snapshot` destroys managed runtime objects and wrappers
first, then releases the graph and its images. Individual unload is unsupported.
This order keeps image-backed strings and native payloads alive for their users.

`module_snapshot_module` exposes the retained validated module. Native consumers
must obtain payloads through its container, not reopen the diagnostic archive
path. Replacing, truncating or deleting an archive after successful preparation
cannot mix new disk data into the running snapshot. A new launch sees the new
contents and performs a new complete validation.

## Consumer-local dependency bindings

Node zero is the root. `module_snapshot_files` maps each preflighted node to its
persistent file scopes; creating a scope does not initialize it.
`module_snapshot_dependency` and `module_snapshot_import` resolve only the
consumer's direct edges into already prepared nodes. Lookup requires exact name
and version, does no I/O, never falls back to another copy, and cannot expose a
transitive or unrelated module without the consumer's direct import. Expected
hashes were verified before any runtime was published. Diamonds reuse one
prepared target. Index enumeration does not flatten the metadata.

## Build identity and physical copies (#235)

The launch snapshot is the instance registry. It contains one node and one runtime
file set per verified `(name, version, mainHash)` identity. Graph discovery indexes
name/version and rejects conflicting expected or actual hashes before constructing
runtime objects; it never silently creates two builds of the same name/version.
`module_snapshot_find` performs an exact, allocation-free inventory lookup of all
three strings. It returns `SIZE_MAX` on any mismatch, without searching the disk,
normalizing versions or granting access to another consumer's dependencies.

All incoming edges for one identity reuse its file scopes, field cells, mutable
state and access identity. A separate version has its own scopes and rights even
if its bytecode matches. Public fields remain module-internal, external fields are
accessible from other versions, and foreign objects cannot be extended. Separate
launch snapshots never share mutable state or access identities.

Archive path, informational metadata and native variants are not part of the
main identity. The search chooses the first candidate as one complete archive;
only that archive's verified native payloads are retained. No later copy is read
to supplement missing or incompatible variants. Changing search order can select
a different complete copy for a new launch, but not augment an existing instance.
Native selection/required mode are supplied by [lazy loading](module-loading-api.md); the [lazy initialization gate](module-initialization-api.md) now owns one-time entry. This registry supplies a single persistent state owner, not a second
initialization mechanism.

This shared loader/runtime preparation API is composed with the include/import
executors and [public CLI](module-cli-api.md), not a second execution mechanism.
`load_module_snapshot_memory` accepts completed archive bytes and a diagnostic
label, applying the same graph preflight and all-or-nothing snapshot constructor.
The snapshot owns its validated bytes after return; the caller can release its
input buffer. The root label is not reopened. The supplied search set still fixes
launch identity and dependency paths. CLI build-and-run needs no temporary
bytecode archive or cleanup process when native is off.

# Снимок запуска в памяти (#192)

`vm/module_snapshot.h` создаёт владеющий снимок запуска поверх общего валидатора
полного графа. `load_module_snapshot` сначала читает и проверяет корневой архив
и все достижимые транзитивные зависимости с единым объектом путей поиска.
Процесс и постоянные области файлов создаются только после `MODULE_GRAPH_OK`.
Отсутствующая, повреждённая или циклическая зависимость, конфликт ожидаемых хешей
не оставляют пригодного для исполнения снимка. Это относится и к зависимостям
из недостижимых ветвей исходного кода.

Необязательный выход диагностики передаёт во владение неисправный граф; иначе
он освобождается автоматически. Ошибка аргументов или памяти очищает оба выхода.
Core сохраняет штатное завершение с сообщением о нехватке памяти или переполнении
размера и ненулевым кодом. Восстановимые отказы выделения общего JSON/ZIP валидатора
возвращают `MODULE_GRAPH_MEMORY`, а не ошибку повреждения. Загрузка немедленно
останавливается без поиска другой копии и подготовки пользовательского кода.
Проверки #236 описаны в [контракте ресурсных отказов](module-memory-api.md).
Частично подготовленный процесс наружу не передаётся. Callback проверки работает
только во время preflight и не должен выполнять пользовательский код или менять
входные данные.

## Владение и неизменяемость

Снимок владеет процессом и проверенным графом. Граф хранит всё распакованное
содержимое выбранных архивов, включая метаданные и нативные части, независимо
от режима native. Каждый модуль получает отдельную оболочку байткода с const
указателями на проверенный образ, без третьей копии образа и без изменения
инструкций, строк и таблиц. Нативные привязки и идентификаторы объявлений находятся
во внешней оболочке; области файлов и состояния инициализации также вне образа.

Все возвращаемые представления заимствованы. Объект путей поиска и настройки
после загрузки больше не нужны. Нельзя отдельно уничтожать возвращённый процесс,
наборы файлов или проверенные модули. После остановки исполнения, всех потоков
и callback `free_module_snapshot` сначала освобождает управляемые объекты и
оболочки, затем граф с образами. Отдельной выгрузки модулей нет. Такой порядок
сохраняет данные строк и нативных частей до завершения их использования.

`module_snapshot_module` предоставляет сохранённый проверенный модуль. Нативные
потребители берут байты из его контейнера, а не открывают диагностический путь
архива заново. Замена, обрезание или удаление .gbin после успешной подготовки
не подмешивает новые данные с диска. Новый запуск увидит новые файлы и заново
проверит полный граф.

## Прямые связи потребителя

Узел ноль — корень. `module_snapshot_files` связывает каждый узел с постоянными
областями его файлов; создание области не выполняет инициализатор.
`module_snapshot_dependency` и `module_snapshot_import` разрешают только прямые
связи конкретного потребителя в уже подготовленные узлы. Нужны точные имя и
версия; чтения файлов, поиска другой копии и доступа к чужой транзитивной
зависимости не происходит. Все ожидаемые хеши проверены до публикации процесса.
Ромб использует один подготовленный общий узел; метаданные не уплощаются.

## Идентичность сборки и физические копии (#235)

Снимок запуска служит реестром экземпляров: одной проверенной тройке
`(name, version, mainHash)` соответствует один узел и один набор файлов runtime.
Поиск графа индексирует имя/версию и отклоняет конфликт ожидаемых или фактического
хеша до создания runtime, а не создаёт две сборки одной версии.
`module_snapshot_find` точно сравнивает все три строки без выделения памяти,
нормализации версий и чтения файлов; несовпадение даёт `SIZE_MAX`. Это инвентарный
API, а не import и не разрешение доступа к чужой зависимости.

Все входящие связи одной сущности используют общие области, ячейки, изменяемое
состояние и идентификатор доступа. Другая версия получает отдельные области и
права даже при совпадении байткода: public остаётся внутренним, external доступен
другим версиям, расширение чужого объекта запрещено. Разные снимки запуска не
разделяют изменяемое состояние или идентификаторы доступа.

Путь архива, информационные метаданные и нативные варианты не входят в основную
идентичность. Выбирается первый найденный архив целиком, сохраняются только его
проверенные нативные части. Поздняя копия не читается для дополнения отсутствующих
или неподходящих вариантов. Новый запуск с другим порядком поиска может выбрать
другую полную копию, но не дополнить уже созданный экземпляр. Нативный выбор и required реализованы [ленивой загрузкой](module-loading-api.md); [машина ленивой инициализации](module-initialization-api.md) уже управляет однократным входом; реестр задаёт
единого владельца постоянного состояния, а не дублирует механизм инициализации.

Этот общий API подготовки используется исполнителями include/import и
[публичным CLI](module-cli-api.md), а не вводит второй механизм исполнения.
`load_module_snapshot_memory` принимает готовые байты архива и диагностическую
метку, используя тот же preflight графа и атомарный конструктор снимка. После
возврата снимок владеет проверенными байтами; исходный буфер можно освободить.
Корень не перечитывается по метке. Переданный поиск по-прежнему фиксирует
идентичность запуска и пути зависимостей. Сборка-запуск CLI при native off
не требует временного архива байткода или процесса уборки.

## Automated checks / Автоматические проверки

`ctest --test-dir build --output-on-failure -L module-snapshot-migration` checks
21 independent archive fixtures: READY-synchronized replacement, deletion and
corruption before VM execution; retained native data; fresh launches; file scopes;
diamonds and versions; direct-only imports; missing/corrupt/hash/identity failures;
an uncaught VM exception; a 512-module chain with a 256 KiB POSIX stack; immutable
image bytes and independent borrowed wrappers. The same suite runs in GitHub CI
on Linux x86-64/AArch64 and Windows UCRT64/MINGW64/MINGW32.

Набор проверяет 21 независимый архивный сценарий: замену, удаление и повреждение
после сигнала READY, но до исполнения VM; сохранение нативных данных; новый запуск;
области файлов; ромбы и версии; только прямые импорты; отказы отсутствия,
повреждения, хеша и идентичности; неперехваченное исключение VM; цепочку из 512
модулей со стеком 256 КиБ на POSIX; неизменяемость образов и независимость оболочек.
Он включён в CI Linux x86-64/AArch64 и Windows UCRT64/MINGW64/MINGW32.

### Identity regression suite (#235)

`ctest --test-dir build --output-on-failure -R "^integration.module_identity\\."` exercises
shared external-cell updates through diamond consumers, separate-version access
boundaries with identical bytecode, independent launch state, exact triple lookup,
identical/informationally different physical copies, first-copy native ownership,
and rejection of conflicting hashes before any runtime is published. It runs with
the existing snapshot suite on every Linux and Windows CI configuration. Tests use
the shared runtime/access APIs; language-level import remains a subsequent task; one-time initialization has
its own [component tests](module-initialization-api.md).

`module_identity_scenarios` проверяет общую external-ячейку потребителей ромба,
раздельные права версий с одинаковым байткодом, независимость запусков, точную
тройку идентичности, физические копии с одинаковыми или различными информационными
метаданными, нативные части только первого архива и отказ при конфликте хешей до
создания runtime. Набор включён во все Linux/Windows задания CI рядом с проверками
снимка. Используется общий API runtime/доступа; языковой import остаётся последующей задачей, а однократная
инициализация проверяется [отдельным набором](module-initialization-api.md).
