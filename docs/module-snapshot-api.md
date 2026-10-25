# In-memory launch snapshot (#192)

`vm/module_snapshot.h` prepares an owned runtime snapshot on top of the shared
whole-graph validator. `load_module_snapshot` first reads and validates the root
and every reachable transitive archive, using the single supplied search-path
snapshot. Only `MODULE_GRAPH_OK` permits creation of a process and its persistent
file scopes. Missing, corrupt, cyclic or hash-conflicting dependencies return no
runnable snapshot, including dependencies in unreachable source branches.

An optional diagnostics output owns the invalid graph; without that output it is
freed automatically. Argument/memory failure clears both outputs. The current
core allocator's fatal-OOM policy is unchanged; bounded-allocation and injected
failure coverage is expanded separately in #236. No partially prepared runtime is
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

This is the shared loader/runtime preparation API, not completed CLI or language
`import` execution. Lazy initialization, the `include`/`import` state machines,
cross-module VM calls and CLI routing remain their ordered tasks
#196/#197/#194/#195/#191/#218. The tests execute already prepared file bytecode
through the existing VM API without claiming those later features.

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
Сохраняется существующая политика аварийного завершения core при нехватке памяти;
расширенные граничные и управляемые отказы проверяются отдельно в #236.
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

Это общий API подготовки загрузчика и runtime, а не готовое исполнение `import`
или CLI. Ленивая инициализация, машины состояний `include`/`import`, межмодульные
вызовы VM и маршрутизация CLI остаются последовательным задачам
#196/#197/#194/#195/#191/#218. Тесты исполняют уже подготовленный байткод файлов
через существующий API VM, не объявляя последующие возможности готовыми.

## Automated checks / Автоматические проверки

`python3 scripts/check_module_snapshot.py build/module_snapshot_testing` checks
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
