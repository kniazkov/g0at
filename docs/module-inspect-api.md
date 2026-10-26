# Module inspector (#212, #223, #214) / Инспектор модулей

## Русский

`goat-inspect` — отдельный исполняемый файл, включённый в обычную сборку и
release-пакет. Он проверяет метаданные и полный достижимый граф зависимостей,
затем пробует подходящие native-библиотеки в отдельных процессах. Байткод и
экспортируемые функции не исполняются; повторного анализа программы нет.
Конструкторы и деструкторы DLL/SO при пробе могут исполняться.

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

Коды завершения: `0` — граф прошёл проверку, в том числе с предупреждениями
о недоступности native; `1` — ошибка чтения, валидации, графа, ABI, очистки
или инфраструктуры;
`2` — неверные аргументы либо профиль цели. Аргументы проверяются до открытия
архива. `--help` и `--version` завершаются с `0`. Общий адаптер `fs_arguments`
сохраняет Unicode-пути Windows без преобразования через ANSI.

### Изолированная проба (#214)

После успешной проверки **всего** графа каждый уникальный модуль получает не более
одной пробы выбранного совместимого варианта. Ошибки не вызывают повторной попытки
или перебора других вариантов. При явной чужой цели выполняются только проверки
метаданных и хешей: загрузка помечается `not_checked`. Совместимость с чужой целью
не проверяется загрузчиком текущей машины и не требует локального C-компилятора.
`--no-probe` не поддерживается.

`--probe-timeout` задаёт положительное целое число секунд (по умолчанию 5);
ноль, переполнение и нецелые значения отвергаются до открытия архива. Один
монотонный срок начинается при попытке запуска дочернего процесса и включает
запуск, загрузку, descriptor, символы, явную выгрузку и завершение процесса.
Вывод и служебные сообщения не продлевают его. Успех требует достоверного
служебного результата, явной выгрузки и нормального завершения с кодом 0 до
дедлайна; одного сообщения об успехе или одного кода 0 недостаточно.

До исполнения библиотечного кода дочерний процесс получает stdin с немедленным
EOF, отдельный временный рабочий каталог и владение всем деревом потомков.
Потомки завершаются при любом исходе пробы до удаления файлов. stdout и stderr
одновременно считываются с начала процесса: отдельно сохраняются первые 65536
байт каждого потока, остальное отбрасывается без остановки чтения. Ровно 65536
байт без продолжения не считаются усечением. Текст экранирует управляющие и
непечатные байты; JSON хранит точные байты как стандартный Base64 с padding без
переносов и отдельный `truncated`. Служебный результат передаётся по другому
каналу; содержимое stdout не может его подменить.

Это **изоляция процесса, не security sandbox**: конструкторы/деструкторы могут
иметь побочные эффекты, в том числе обращаться к абсолютным путям вне временного
каталога. Проба не доказывает безопасность библиотеки или успех будущего запуска.

Общая внешняя служба очистки запускается до извлечения и исполнения, переживает
аварийное завершение инспектора и удаляет только принадлежащие ему временные
данные. Ожидание подтверждённого результата очистки ограничено отдельными
5 секундами; служба имеет общий 30-секундный срок повторов удаления занятых файлов.
Истечение клиентского ожидания не отменяет и не перезапускает очистку. Ошибка
очистки сохраняется отдельно от исхода пробы; известный временный каталог
указывается для диагностики. После тайм-аута служба ещё может работать, поэтому
немедленное ручное удаление небезопасно. Старые и посторонние каталоги не очищаются.

Ошибки загрузчика/выгрузки, авария и тайм-аут пробы — предупреждения при корректном
графе и успешной очистке: исполнение через VM остаётся возможным, но не обещанным.
Несовместимый descriptor или отсутствующий объявленный символ — ошибка ABI;
ошибки инфраструктуры, захвата и очистки тоже дают код 1.

### Форматы и проверки

`--json` выводит один отчёт `reportVersion: 1`, включая штатные ошибки и
справку/версию. [Схема и каталог кодов](module-inspect-report-v1.md) разделяют
метаданные, невыполненные проверки, результат пробы и результат очистки. JSON
не содержит отдельного списка причин несовместимости; они остаются в тексте.
Без выполненной пробы захваты вывода и успех загрузки не выдумываются.

Проверка метаданных CLI: `python3 scripts/check_module_inspect.py build/goat-inspect`.
Общие наборы native, валидации, дочерних процессов и службы очистки дополняют
сквозные проверки изолированных проб: стадии загрузки/выгрузки, ABI, бинарный
вывод, дедлайны, EOF stdin, единичная проба общего узла, аварии, отказы запуска,
гибель владельца после готовности уборщика и отдельная ошибка ожидания очистки
с продолжающейся уборкой. Инъекции отказов собраны только в тестовом executable;
рабочая утилита их не поддерживает. JSON проверяется по полной схеме:

```sh
python3 scripts/check_inspect_probe.py build/goat-inspect build/module_import_execution_testing cc build/inspect_probe_testing
```

## English

`goat-inspect` is a separate executable included in normal builds and release
packages. It validates metadata and the entire reachable dependency graph, then
probes matching native libraries in separate processes. It executes neither
bytecode nor exported functions and does not repeat program analysis. DLL/SO
constructors and destructors may execute during a probe.

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

Handled exit statuses are `0` for a valid graph, including native-availability
warnings; `1` for read, validation, graph, ABI, cleanup or infrastructure errors;
and `2` for invalid arguments or target profiles. Arguments are validated
before opening the archive. `--help` and `--version` return `0`. The common
`fs_arguments` adapter preserves Windows Unicode paths without ANSI round trips.

### Isolated probe (#214)

Only after successful validation of the **entire** graph does each unique module
receive at most one probe of its selected compatible variant. Failures never
cause retries or fallback to another variant. An explicitly foreign target gets
metadata/hash checks only, with loading marked `not_checked`. Compatibility with
a foreign target is not tested using the local loader and needs no local C
compiler. `--no-probe` is unsupported.

`--probe-timeout` accepts positive integer seconds (default 5); zero, overflow
and nonintegers are rejected before opening the archive. A single monotonic
deadline starts at the attempted child launch and includes launch, load,
descriptor, symbols, explicit unload and process termination. Output and service
messages never extend it. Success requires a valid service result, explicit
unload and normal child exit 0 before the deadline; a success message alone or
exit 0 alone is insufficient.

Before any library code runs, the child receives immediate-EOF stdin, a separate
temporary working directory and ownership of its entire descendant tree.
Descendants are stopped on every probe outcome, before files are deleted.
stdout and stderr are drained concurrently from process start: the first 65536
bytes of each are retained independently, with further bytes discarded while
reading continues. Exactly 65536 bytes without more output is not truncation.
Text escapes control/nonprintable bytes; JSON preserves exact bytes as standard
padded Base64 without line breaks plus a separate `truncated` flag. The service
result uses a separate channel; stdout content cannot impersonate it.

This is **process isolation, not a security sandbox**: constructors/destructors
can have side effects, including access to absolute paths outside the temporary
directory. A probe proves neither library safety nor future program success.

The shared external cleanup service starts before extraction/execution, survives
inspector crashes and deletes only owned temporary data. Waiting for its actual
cleanup result has a separate 5-second limit; the service has one 30-second retry
budget for deleting busy files. Client timeout neither cancels nor restarts
cleanup. Cleanup failure is reported separately without replacing the probe
outcome; the known temporary directory is included for diagnosis. After timeout
the service may still be working, so immediate manual deletion is unsafe. Old
and unrelated directories are not scavenged.

Load/unload errors, crashes and probe timeouts are warnings when graph validation
and cleanup succeed: VM execution may remain possible, but is not guaranteed.
An incompatible descriptor or missing declared symbol is an ABI error;
infrastructure, capture and cleanup failures also return 1.

### Formats and tests

`--json` emits one `reportVersion: 1` report, including handled errors and
help/version. The [schema and code catalog](module-inspect-report-v1.md) separate
metadata, skipped checks, probe outcome and cleanup outcome. JSON has no separate
incompatibility-reason list; those explanations remain in text. Unperformed
probes never produce invented captured streams or loading successes.

Run `python3 scripts/check_module_inspect.py build/goat-inspect` for metadata CLI
coverage. Shared native, validation, child-process and cleanup-service suites
complement end-to-end isolated-probe coverage: load/unload stages, ABI, binary
output, deadlines, EOF stdin, one probe per shared node, crashes, startup failures,
owner death after janitor readiness, and separate cleanup-wait failure with
continuing cleanup. Fault hooks are compiled only into the test executable, not
the production utility. JSON is checked against the full report schema:

```sh
python3 scripts/check_inspect_probe.py build/goat-inspect build/module_import_execution_testing cc build/inspect_probe_testing
```
