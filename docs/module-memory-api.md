# Module loading resource failures (#236)

`load_module_snapshot` publishes no runtime until the complete graph and its
runtime scopes have been prepared. A JSON/ZIP allocation failure is preserved as
`MODULE_VALIDATION_MEMORY`, immediately stops graph discovery, and returns
`MODULE_GRAPH_MEMORY` with both outputs cleared. It is not evidence of corrupt
input and never authorizes trying a different copy. Core's existing fatal
allocation policy remains: exhaustion or size overflow prints a diagnostic and
exits with `EXIT_FAILURE`, without executing user code.

`allocation_size_add` and `allocation_size_multiply` check before calculation.
The loader checks dynamic arrays, table growth, bytecode counts, ZIP entry totals
and native arrays against `size_t`. ZIP32 and caller-provided archive/expanded
limits are unchanged. Correctly structured data too large for a process is a
resource limitation, not another wire format. No additional arbitrary module or
graph-depth limit is introduced.

## Controlled tests

Configure a separate test-only tree with `-DGOAT_TEST_ALLOC_FAILURE=ON`, build
`module_memory_scenarios`, then run
`ctest --test-dir <build> --output-on-failure -L module-memory-migration`.
Run separate Debug and Release configurations; see the [scenario map and direct
invocation](../test/data/module-memory/README.md).
The option instruments malloc/calloc/realloc in core, shared module validation,
yyjson and ZIP/zlib. Production builds default to OFF and expose no fault knob.
The injector starts only at the loading boundary after search preparation.
Subprocesses test each observed allocation with both a single failure and sticky
exhaustion, and test synthetic size boundaries without gigabyte allocations.
No fault may lead to an executable partial snapshot or selection of a fallback
copy. Tests run in the existing Linux x86-64/AArch64 and all three Windows jobs,
including MINGW32. They do not claim the later native/CLI integration tasks.

# Ресурсные отказы загрузки модулей (#236)

`load_module_snapshot` не передаёт runtime до полной проверки графа и подготовки
областей файлов. Ошибка выделения JSON/ZIP сохраняется как
`MODULE_VALIDATION_MEMORY`, немедленно прекращает обход и даёт
`MODULE_GRAPH_MEMORY` с очищенными выходами. Это не повреждение входа и не повод
искать другую физическую копию. Для core действует прежняя политика: сообщение
о нехватке памяти или переполнении размера и `EXIT_FAILURE`, без пользовательского
исполнения.

`allocation_size_add` и `allocation_size_multiply` проверяют арифметику до
вычисления. Проверяются массивы, рост таблиц, счётчики байткода, сумма размеров ZIP
и нативные массивы с учётом `size_t`. Ограничения ZIP32 и заданные вызывающим
лимиты архивов/распаковки не меняются. Нехватка адресного пространства процесса
не создаёт новую версию байткода. Произвольные лимиты числа модулей и глубины
графа не добавлены.

Отдельная тестовая сборка `GOAT_TEST_ALLOC_FAILURE=ON` подменяет выделения core,
JSON и ZIP/zlib только для проверки. В обычной сборке механизм выключен и нет
runtime-переключателя отказов. Тест перебирает точки выделения после подготовки
путей поиска: однократный отказ и продолжающуюся нехватку памяти. Границы размеров
проверяются искусственными входами без гигабайтных буферов. Частичный запуск и
выбор запасной копии запрещены. Проверки включены в существующие Linux/ARM и
Windows CI, включая MINGW32; интеграция CLI и native остаётся последующим задачам.
