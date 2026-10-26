# Metadata inspector (#212) / Просмотр метаданных

## Русский

`goat-inspect` — отдельный исполняемый файл, включённый в обычную сборку и
release-пакет. Этап #212 предоставляет текстовую инспекцию метаданных и полного
достижимого графа зависимостей. Он не исполняет байткод, не вызывает экспортируемые
функции, не извлекает и не загружает DLL/SO и не повторяет анализ программы.

```sh
goat-inspect module.gbin
goat-inspect --module-dir modules --target target.json module.gbin
goat-inspect -- --literal-name
```

Принимается ровно один буквальный путь архива с любым расширением или без него.
Суффикс `.gbin` не добавляется. Для зависимостей используется общий поиск от
канонического корневого архива: соседний `modules`, единственный явный
`--module-dir`, затем `GOAT_MODULE_PATH`. Сам модуль, его зависимости, закреплённые
хеши, циклы и внешние ссылки проверяются общим валидатором графа. Один общий узел
не проверяется повторно по каждой ведущей к нему ветви.

`--target` принимает путь строгого JSON-профиля `target.schema.json`; по умолчанию
используются обнаруженные свойства текущего процесса. Профиль задаёт доступные
параметры ОС, архитектуры, runtime и CPU, а не выбирает другую физическую копию
архива. Совместимость каждого варианта вычисляет общий API
`goat_module_native_compatibility`, используемый также селектором. Текст объясняет
независимые причины отказа, включая неизвестные обязательные версии и недостающие
CPU-возможности. Соответствие метаданным не означает успешную загрузку библиотеки.

Вывод содержит имя, версию, основной хеш, зависимости, информационные поля,
всех `producers`, варианты с `cCompiler` и список `externals`. Экспорты упорядочены
по декодированным UTF-8 именам; рядом показываются `mutable` и имеющийся `type`.
Отсутствующий тип явно обозначается как `unknown`, без анализа начального значения.
Управляющие символы экранируются, а порядок вывода воспроизводим при одинаковых
данных и среде проверки.

Общий валидатор передаёт через `module_validation_options_t.metadata` заимствованный
нормализованный JSON-документ только после успешной структурной проверки метаданных.
Отдельный `metadata_context` сохраняется при обёртывании callback валидатором графа.
Инспектор копирует документ для отчёта: пригодные метаданные остаются видны даже
при ошибке хеша или байткода. Это не исполняемый частичный снимок и не подтверждение
целостности архива. При ошибке JSON/схемы сведения не выдумываются; неисполненные
проверки и причины показываются общим диагностическим API отдельно от ошибок.

Коды завершения этого этапа: `0` — граф прошёл проверку, в том числе без подходящего
native-варианта; `1` — ошибка чтения, валидации, графа или инфраструктуры вывода;
`2` — неверные аргументы либо профиль цели. Аргументы проверяются до открытия
архива. `--help` и `--version` завершаются с `0`. Общий адаптер `fs_arguments`
сохраняет Unicode-пути Windows без преобразования через ANSI.

### Граница реализации

Полный согласованный контракт inspect остаётся в книге. В последовательности
чеклиста #223 реализует окончательную версионированную JSON-схему и машиночитаемый
отчёт; сейчас `--json` явно отвергается с кодом `2`, временная схема не вводится.
#214 добавит изолированную пробную загрузку. Пока отчёт всегда явно сообщает
`Loading: not checked`; успешная проверка метаданных не выдаётся за успешную пробу.
`--no-probe` не поддерживается. `--probe-timeout` уже проверяет положительное целое
число секунд с контролем переполнения (будущее значение по умолчанию — 5), но на
этом этапе не запускает процесс пробы и не ограничивает исполнение программы.

Проверка CLI: `python3 scripts/check_module_inspect.py build/goat-inspect`.
Общие наборы проверки native и валидации дополнительно покрывают причины
несовместимости и безопасный metadata callback. Настоящие JSON-отчёты и OS-пробы
не объявляются проверенными этим набором.

## English

`goat-inspect` is a separate executable included in normal builds and release
packages. Step #212 supplies text inspection of metadata and the entire reachable
dependency graph. It neither executes bytecode or exported functions nor extracts
or loads DLL/SO files, and does not repeat program analysis.

```sh
goat-inspect module.gbin
goat-inspect --module-dir modules --target target.json module.gbin
goat-inspect -- --literal-name
```

Exactly one literal archive path is accepted, with any extension or none. No
`.gbin` suffix is appended. Dependencies use the shared search relative to the
canonical root: neighboring `modules`, one explicit `--module-dir`, then
`GOAT_MODULE_PATH`. Shared graph validation checks archives, pinned hashes, cycles
and external references. A shared dependency is not revalidated for each incoming
branch.

`--target` accepts a strict `target.schema.json` profile path; the default uses
detected properties of the current process. The profile describes available OS,
architecture, runtime and CPU facts, not a different physical archive selection.
Per-variant compatibility uses `goat_module_native_compatibility`, also used by
the selector. Text explains independent mismatches, including unknown mandatory
versions and missing CPU capabilities. Matching metadata does not prove library
loading.

Output includes identity, main hash, dependencies, informational fields, every
`producers` entry, native variants with their `cCompiler`, and `externals`.
Exports are ordered by decoded UTF-8 names and show `mutable` and any declared
`type`. An absent type is explicitly `unknown`, without inference from an initial
value. Control characters are escaped; ordering is reproducible for identical
inputs and checking conditions.

The shared validator's `module_validation_options_t.metadata` callback receives
a borrowed normalized JSON document only after structural metadata validation.
Its separate `metadata_context` survives graph-report callback wrapping. The
inspector copies that document for reporting, so safe metadata remains visible
after a hash or bytecode failure. It is neither a partial executable snapshot nor
proof of archive integrity. Invalid JSON/schema does not produce invented module
information. Shared diagnostics distinguish failed checks from skipped checks
and explain prerequisites.

Handled exit statuses at this stage are `0` for a valid graph, including no
compatible native variant; `1` for read, validation, graph or output-infrastructure
errors; and `2` for invalid arguments or target profiles. Arguments are validated
before opening the archive. `--help` and `--version` return `0`. The common
`fs_arguments` adapter preserves Windows Unicode paths without ANSI round trips.

### Implementation boundary

The complete agreed inspect contract remains in the book. The checklist assigns
the final versioned JSON schema and machine-readable renderer to #223. Until
then, `--json` explicitly returns `2`; no temporary report schema is invented.
Isolated native load probing belongs to #214. Current reports always state
`Loading: not checked`, never presenting metadata validation as a successful
probe. `--no-probe` is unsupported. `--probe-timeout` already validates positive
integer seconds with overflow checks (the future default is 5), but this stage
starts no probe and imposes no program-execution timeout.

Run `python3 scripts/check_module_inspect.py build/goat-inspect` for CLI coverage.
The shared native/validation suites additionally cover compatibility explanations
and safe metadata callbacks. This coverage does not claim JSON reports or OS
probes have been implemented or tested.
