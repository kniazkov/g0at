# Ready-module import execution / Исполнение import готовых модулей (#195)

## English

`compile_module_sources_with_dependencies` consumes immutable source snapshots and
verified direct bindings from `bind_source_dependencies`. The existing syntax
collector resolves literal names and exact versions, including imports inside
included files, functions and unreachable branches. A versionless name requires
its descriptor's explicit default; an explicit version overrides that default
only at that occurrence. Dynamic operands and paths to internal files are errors.
The supplied bindings must match the complete collected set, with no missing or
extra pairs. Only direct name/version/hash records enter the generated metadata;
a library's own dependencies are never replaced by consumer defaults.

The compiler emits `MODULE_IMPORT` (opcode 74, already reserved by bytecode v1)
with a canonical `name@version` string descriptor. There is no new wire version,
filesystem lookup during code generation, or implicit latest-version selection.
`compile_module_sources` remains the no-dependencies convenience wrapper. The
compilation implementation belongs to `goat_validation`, alongside dependency
binding and packaging, rather than introducing a core-to-loader link cycle.

Before publication, the final temporary archive must pass `validate_module_graph`
with the original startup search object. Source-graph validation alone cannot
exclude a dependency back to the newly named build. Before execution,
`load_module_snapshot` validates the entire reachable graph and retains all
images. Each import instruction is then bound outside the image to the **main
file** of a direct dependency of its declaring module. The global identity cache
is not an access grant; transitive dependencies are not implicitly direct imports.

Execution calls the existing file-initialization gate and returns the persistent
main-file scope. Repeated imports of the same retained identity share state;
different versions do not. Initialization is lazy, caller locals are not inherited,
and a failed initialization rethrows the same saved value and original trace
without repeating effects. Access remains based on declaration provenance:
`public` is module-local and `external` permits cross-module access. Merely
returning the entry scope does not expose private/internal file cells.

The VM selects the image of each executing lexical context, including imported
functions, returned closures and callbacks into a caller. Each module keeps its
own GC-rooted string-descriptor cache outside immutable bytecode, so equal numeric
descriptor/file indices in different modules cannot mix values or permissions.
Nested import/include execution restores the caller's image and operand state.
Images remain owned by the snapshot until its process and caches are destroyed;
no archive or source is reopened while executing an already loaded graph.

`module_import_execution_testing` is an internal integration harness:

- `compile SOURCE OUTPUT`: collect/bind/compile, package to a temporary archive,
  validate the final graph, then publish;
- `binary ARCHIVE`: load the full graph and execute its entry file;
- `frozen ARCHIVE`: load first, print `READY`, wait for stdin, then execute.

Run `python3 scripts/check_module_import_execution.py build/module_import_execution_testing`.
The tests cover exact/default versions, repeated and transitive identity, lazy
initialization, cross-module access/functions/callbacks/closures, nested includes,
operand preservation, saved failures, invalid literals/internal paths, complete
preflight of dead-code dependencies, changed hashes and execution after every
archive/source is deleted. All retained image bytes are compared after execution.
The same suite runs on every Linux and Windows CI target. Public CLI integration
(#191), interface analysis (#205) and re-export declarations (#228) remain separate
checklist steps; this change does not add public command-line switches.

## Русский

`compile_module_sources_with_dependencies` получает неизменяемые снимки исходников
и проверенные прямые связи из `bind_source_dependencies`. Общий сборщик синтаксиса
разрешает литеральные имена и точные версии, включая импорты из включённых файлов,
функций и невыполняемых ветвей. Имени без версии необходимо явное значение по
умолчанию в дескрипторе; явная версия переопределяет его только в данном месте.
Вычисляемые операнды и пути к внутренним файлам запрещены. Переданные связи должны
точно совпадать с полным собранным набором, без пропусков и лишних пар. В метаданные
попадают только прямые name/version/hash; настройки потребителя не меняют
собственные зависимости библиотеки.

Компилятор создаёт `MODULE_IMPORT` (код 74, уже зарезервированный в bytecode v1)
с дескриптором канонической строки `name@version`. Новая версия формата, файловый
поиск при генерации и неявный выбор последней версии не вводятся.
`compile_module_sources` остаётся обёрткой для исходников без зависимостей.
Реализация компиляции входит в `goat_validation` вместе со связыванием и упаковкой,
не создавая циклическую зависимость core от загрузчика.

Перед публикацией итоговый временный архив обязательно проверяется через
`validate_module_graph` с исходным поисковым снимком. Проверка графа исходников
сама по себе не исключает обратной зависимости от имени собираемого модуля.
Перед исполнением `load_module_snapshot` проверяет весь достижимый граф и
удерживает образы. Каждый import связывается вне образа с **входным файлом**
прямой зависимости объявляющего модуля. Глобальный кэш экземпляров не предоставляет
прав на импорт; транзитивная зависимость автоматически прямой не становится.

Исполнение использует существующий шлюз инициализации и возвращает постоянную
область входного файла. Повторные импорты одного удерживаемого экземпляра разделяют
состояние, разные версии независимы. Инициализация ленивая, локальные переменные
вызывающего кода не наследуются. Неудача повторно выбрасывает то же сохранённое
значение с исходной трассировкой, не повторяя эффекты. Права определяет место
объявления: `public` действует внутри модуля, `external` — между модулями.
Возврат входной области не раскрывает скрытые ячейки и внутренние файлы.

VM выбирает образ текущего лексического контекста, включая импортированные
функции, возвращённые замыкания и обратные вызовы потребителя. Каждый модуль имеет
собственный защищённый от GC кэш строковых дескрипторов вне неизменяемого байткода.
Одинаковые числовые индексы строк и файлов разных модулей не смешивают значения
или права. Вложенный import/include восстанавливает образ и стек вызывающего кода.
Снимок удерживает образы до уничтожения процесса и кэшей; при исполнении уже
загруженного графа архивы и исходники повторно не открываются.

Внутренний компонент `module_import_execution_testing` предоставляет команды:

- `compile SOURCE OUTPUT`: собрать и связать импорты, скомпилировать, упаковать
  временный архив, проверить итоговый граф и только затем опубликовать;
- `binary ARCHIVE`: загрузить граф и исполнить его входной файл;
- `frozen ARCHIVE`: загрузить граф, вывести `READY`, дождаться stdin и исполнить.

Запуск: `python3 scripts/check_module_import_execution.py build/module_import_execution_testing`.
Проверяются точные версии и умолчания, повторная и транзитивная идентичность,
ленивая инициализация, межмодульные права, функции, callbacks и замыкания,
вложенные include, сохранение операндов и ошибок, неверные литералы и внутренние
пути, полный preflight невыполняемых зависимостей, изменённые хеши и исполнение
после удаления всех архивов и исходников. Байты всех образов сравниваются после
исполнения. Набор выполняется на каждой Linux/Windows-платформе CI. Публичная CLI
(#191), анализ интерфейсов (#205) и объявления реэкспорта (#228) остаются отдельными
шагами; новых пользовательских флагов здесь нет.
