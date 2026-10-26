# goat-inspect JSON report v1 / JSON-отчёт goat-inspect v1

## English

The normative closed JSON Schema is
[`module-format-v1/inspect-report.schema.json`](module-format-v1/inspect-report.schema.json).
Its `reportVersion: 1` is independent of the module format, bytecode, native ABI,
analysis and utility versions. The report is not an archive manifest and is not
hashed into the module. Existing field meanings and diagnostic codes cannot
change incompatibly within this version.

`goat-inspect --json archive` writes one UTF-8 JSON document and a newline to
stdout, including on handled read, validation and argument failures. It writes
no text banner or copied child output to that stream. `--json` is recognized
throughout option parsing before `--`; a literal filename after `--` is not an
option. Invalid arguments and invalid target profiles return 2 before archive
inspection; read, archive, graph and infrastructure errors return 1; success,
including native-availability warnings, returns 0. External termination,
unrecoverable allocation failure or a broken output stream cannot promise a
complete report. A failed write returns 1 when control remains available.

### Complete structure

Every report has all seven fields:

- `reportVersion`: integer 1.
- `success`: boolean, true exactly when `exitCode` is 0.
- `exitCode`: integer 0, 1 or 2, with the meanings above.
- `input`: supplied literal input path, or null when no path was supplied.
- `target`: the checked environment, or null when unavailable/not needed.
- `modules`: one record for each discovered module, including failed nodes.
- `diagnostics`: ordered errors, warnings and skipped checks. An empty array
  means no emitted diagnostics, not that an unperformed check succeeded.

`--json --help` and `--json --version` use the same envelope, with an additional
`information` object containing `kind` (`help` or `version`) and `text`.

A target contains `source` (`current` or `explicit`), `os`, `architecture`,
`bits`, `littleEndian`, `runtime`, `osVersion`, `runtimeVersion`, and
`cpuFeatures`. Versions are arrays of four unsigned 32-bit integers, or null
when unknown; zero is a known component, not a substitute for unknown. CPU
features are canonical names in deterministic bit order. The target describes
available properties, not the minimum requirements stored in native metadata.
An unknown OS, architecture or runtime is the string `unknown`.

Each module contains `archive`, `validation` (`passed` or `failed`), `metadata`,
`exports`, and `native`. `metadata` is the complete normalized, structurally
validated module metadata object. It may remain available after hash/bytecode
failure; `validation: failed` and diagnostics still forbid execution. If safe
metadata is unavailable, all three of `metadata`, `exports`, and `native` are
null, not fabricated empty objects or arrays. Dependencies, identity, hashes,
producers and optional informational fields are obtained from `metadata`.
Empty optional information is normalized by the common reader; whitespace and
nonempty strings retain their exact values. Integer metadata remains integer,
including values beyond the exact range of binary64.

`exports` is ordered by unsigned decoded UTF-8 name bytes. Each record has
`name`, boolean `mutable`, and optional `type`; an unknown type is omitted,
not inferred or replaced with a fictional type. A known empty interface is `[]`.
Each `native` record has `library`, `compatibility: {state}`, and `loading`.
Compatibility states are `compatible`, `incompatible`, and `not_checked`.
No separate JSON list of incompatibility reasons is introduced; the text
formatter supplies those explanations using the common selector. Requirements,
ABI, compiler and specialization metadata remain in `metadata.native`.

An unperformed probe has `loading: {state: "not_checked", reason: {code,
message}}`. Reasons distinguish `VALIDATION_FAILED` (the entire graph must pass),
`FOREIGN_TARGET`, `NO_COMPATIBLE_VARIANT`, `AMBIGUOUS_NATIVE_VARIANT`, and
`NOT_SELECTED`. No captured streams, cleanup successes or child exit statuses
are invented for an unperformed probe. An explicit target differing from the
current environment is metadata-only. Bytecode and exported user functions are
never executed; a performed probe can execute DLL/SO constructors/destructors.

A performed isolated probe has `loading.state: "checked"`, with separate `probe`
and `cleanup` objects. A probe has `state` (`passed`, `failed`, `crashed`,
`timeout`), `stage` (`start`, `load`, `descriptor`, `symbols`, `unload`, `exit`,
`capture`), nullable integer `exitCode` and `signal`, `capture`, and `diagnostics`.
`completedStages` lists successfully completed `load`, `descriptor`, `symbols`
and `unload` stages in order; it does not imply that later termination succeeded.
Successful loading is not successful probe completion until explicit unload,
a valid service result and normal child termination succeed within the original
deadline. One selected variant receives at most one attempt per unique module.

`capture.stdout` and `capture.stderr` each contain `dataBase64` (standard padded
Base64, without line breaks) and boolean `truncated`. Each is limited to the
first 65536 bytes independently; exactly 65536 bytes are not truncated unless
additional bytes arrived. Arbitrary bytes survive decoding unchanged. A cleanup
object has `state` (`passed` or `failed`), its own `diagnostics`, and
`remainingPaths`; cleanup failure never replaces the original probe result.
On failure, optional `temporaryDirectory` identifies the known absolute owned
directory; it is omitted if no path is known. `remainingPaths: []` does not claim
the directory is absent when cleanup failed: no current existence is asserted
without evidence. After the separate 5-second client wait times out, the janitor
may still be retrying within its 30-second budget; immediate manual deletion is
unsafe. See the [process-isolation limits and cleanup contract](module-inspect-api.md).

### Diagnostics and deterministic ordering

Every diagnostic contains `severity` (`error`, `warning`, or `skipped`),
nonempty stable `code`, nonempty human-readable `message`, and `path`.
Messages can be translated or refined; consumers match `code`, never prose or
OS error text. `skipped` means an unmet prerequisite, not a failed attempted
check and not a success. Archive `CHECK_SKIPPED` diagnostics carry a
`prerequisite` object with `check` and `containerCheck` separately. Graph
`REFERENCE_SKIPPED` diagnostics use `check: "references"` and dependency `path`
without an archive `prerequisite` object. Check names are `input`, `container`,
`bytecode`, `native`, and `references`; archive prerequisite checks use the
first four names. Container checks are `layout`, `content`, `json`,
`declarations`, `native_hash`, and `main_hash`.

Optional locations/details are `check`, `archive`, `entryBase64`, `field`,
`index`, `reference`, `systemError`, `prerequisite`, `expectedHash`, and
`actualHash`. `entryBase64` represents exact ZIP-name bytes, which need not be
valid UTF-8. JSON pointers and references retain their valid JSON strings,
including escaped controls/NUL. A diagnostic path is an array of steps with
nullable `name`, `version`, and `expectedHash`; it identifies the dependency
chain without relying on discovery-order indices. Root-only diagnostics have
an empty path. Full hashes are retained, not shortened.

Modules use the same stable name/version/archive ordering as the text report.
Graph diagnostics use the common deterministic path ordering, preserving every
relevant incoming path while validating each unique module once. No independent
inspector validator or re-analysis is introduced. Invalid JSON does not create
missing-field or main-hash errors from an invented object. Validation continues
only where prerequisites permit it, and skipped checks remain structural.

### Stable code catalog

| Code | Meaning |
| --- | --- |
| `INVALID_ARGUMENT` | Invalid command arguments. |
| `TARGET_IO` | Target profile cannot be read. |
| `TARGET_INVALID` | Target profile syntax, structure or semantics is invalid. |
| `ARCHIVE_IO` | Root/archive/search-path filesystem access failed. |
| `RESOURCE_LIMIT` | Validation cannot proceed within a configured/address-space limit. |
| `OUT_OF_MEMORY` | A handled allocation failure prevented inspection. |
| `ZIP_INVALID` | ZIP framing, compression or integrity validation failed. |
| `CONTAINER_INVALID` | Archive layout, path, entry declarations or types are invalid. |
| `JSON_INVALID` | Metadata JSON parsing, types, schema or normalization failed. |
| `MAIN_HASH_MISMATCH` | Stored main hash differs from the recomputed main hash. |
| `NATIVE_HASH_MISMATCH` | A native payload differs from its declared hash. |
| `BYTECODE_INVALID` | Bytecode image or its metadata cross-links are invalid. |
| `NATIVE_INVALID` | Native metadata or internal consistency is invalid. |
| `CHECK_SKIPPED` | A validation check could not run because its prerequisite failed. |
| `DEPENDENCY_NOT_FOUND` | An exact required module/version was not found. |
| `MODULE_INVALID` | A selected dependency is invalid; no later search candidate substitutes it. |
| `IDENTITY_MISMATCH` | Selected metadata does not match the requested name/version. |
| `DEPENDENCY_HASH_MISMATCH` | Actual dependency main hash differs from the expected hash. |
| `DEPENDENCY_HASH_CONFLICT` | Incoming requests pin conflicting hashes for one identity. |
| `DEPENDENCY_CYCLE` | A dependency cycle exists. |
| `REFERENCE_INVALID` | An external function reference fails whole-graph validation. |
| `REFERENCE_SKIPPED` | Reference checking depends on an unresolved/failed graph edge. |
| `NO_NATIVE_VARIANT` | No native variant matches the selected environment; VM remains possible. |
| `AMBIGUOUS_NATIVE_VARIANT` | More than one variant matches; no arbitrary winner is chosen. |
| `REPORT_FAILED` | Handled report construction/infrastructure failure. |
| `PROBE_ABI_INVALID` | Loaded descriptor or declared symbol violates the native ABI (error). |
| `PROBE_INFRASTRUCTURE_FAILED` | Probe launch, service protocol or capture infrastructure failed (error). |
| `PROBE_CRASHED` | Probe process crashed (warning). |
| `PROBE_TIMEOUT` | Probe did not finish within its original deadline (warning). |
| `PROBE_LOAD_FAILED` | OS loader could not load the selected library (warning). |
| `PROBE_UNLOAD_FAILED` | Explicit library unload failed (warning). |
| `PROBE_FAILED` | Other unsuccessful probe termination (warning). |
| `CLEANUP_TIMEOUT` | Cleanup result wait expired; the janitor may still be working (error). |
| `CLEANUP_FAILED` | Owned process tree or temporary artifacts could not be cleaned (error). |

All diagnostic objects retain nonempty code/message requirements;
platform errno or loader text is never a stable code. Unknown future codes must
not cause consumers to misclassify severity or skipped state.

## Русский

Нормативная закрытая JSON Schema находится в
[`module-format-v1/inspect-report.schema.json`](module-format-v1/inspect-report.schema.json).
`reportVersion: 1` не зависит от версий модуля, байткода, анализа, native ABI и
утилиты. Отчёт не является manifest архива и не входит в основной хеш.
Несовместимое изменение смысла полей или назначенных кодов требует новой версии.

`goat-inspect --json archive` выводит в stdout ровно один документ UTF-8 JSON
и перевод строки, в том числе при штатно обработанных ошибках. Баннеры,
диагностика вне JSON и вывод дочернего процесса туда не примешиваются.
`--json` распознаётся среди опций до `--`; после `--` это буквальное имя файла.
Ошибки параметров и профиля цели дают 2 до проверки архива; ошибки чтения,
архива, графа и инфраструктуры дают 1; успех, в том числе с предупреждениями
о недоступности native, даёт 0. Внешнее завершение процесса, неустранимый OOM
и разрыв выходного потока не обещают целого отчёта. Обработанная ошибка записи
даёт 1, если управление осталось у утилиты.

### Полная структура

Каждый отчёт содержит все семь обязательных полей:

- `reportVersion`: целое 1.
- `success`: логическое значение, истинное ровно при `exitCode == 0`.
- `exitCode`: целое 0, 1 или 2 с описанным выше смыслом.
- `input`: переданный буквальный путь либо null, если путь не задан.
- `target`: проверяемая среда либо null, если она недоступна/не требуется.
- `modules`: записи всех обнаруженных модулей, включая неуспешные узлы.
- `diagnostics`: упорядоченные ошибки, предупреждения и пропуски проверок.

Справка и версия с `--json` сохраняют эту оболочку и добавляют `information`
с `kind` (`help` или `version`) и `text`.

Цель содержит `source` (`current`/`explicit`), `os`, `architecture`, `bits`,
`littleEndian`, `runtime`, `osVersion`, `runtimeVersion`, `cpuFeatures`.
Версии — четыре беззнаковых 32-битных компонента либо null при неизвестной
версии. Ноль — известный компонент, а не замена неизвестному. CPU-возможности
записываются каноническими именами в стабильном порядке битов. Это доступные
свойства среды, не минимальные требования варианта. Неизвестные ОС, архитектура
и runtime обозначаются `unknown`.

Модуль содержит `archive`, `validation` (`passed`/`failed`), `metadata`,
`exports`, `native`. `metadata` — полный нормализованный объект метаданных,
прошедший структурную проверку. Он может сохраниться после ошибки хеша или
байткода; `validation: failed` и диагностики по-прежнему запрещают исполнение.
Если безопасных метаданных нет, `metadata`, `exports` и `native` равны null,
а не выдуманным пустым коллекциям. Идентичность, хеши, зависимости, producers
и информационные поля находятся в `metadata`. Общий читатель удаляет пустую
необязательную информацию, сохраняет пробелы и точные непустые строки.
Целые значения остаются целыми, включая числа вне точного диапазона binary64.

`exports` упорядочен по беззнаковым байтам декодированных UTF-8 имён. Запись
содержит `name`, логическое `mutable` и необязательный `type`. Неизвестный тип
не выводится и не угадывается; известный пустой интерфейс — `[]`. Запись
`native` содержит `library`, `compatibility: {state}` и `loading`.
Состояние совместимости: `compatible`, `incompatible`, `not_checked`.
Отдельный перечень причин несовместимости в JSON не вводится; их объясняет
текстовый форматтер через общий селектор. Требования, ABI, компилятор и
специализации сохраняются в `metadata.native`.

Невыполненная проба имеет вид `loading: {state: "not_checked", reason: {code,
message}}`. Причины: `VALIDATION_FAILED` (нужна проверка всего графа),
`FOREIGN_TARGET`, `NO_COMPATIBLE_VARIANT`, `AMBIGUOUS_NATIVE_VARIANT`,
`NOT_SELECTED`. Захваты stdout/stderr, успешная очистка и коды дочернего процесса
для невыполненной пробы не выдумываются. Явная цель, отличающаяся от текущей
среды, проверяется только по метаданным. Байткод и экспортируемые функции не
исполняются; выполненная проба может исполнять конструкторы/деструкторы DLL/SO.

Выполненная изолированная проба имеет `loading.state: "checked"` с отдельными
`probe` и `cleanup`. Проба содержит `state` (`passed`, `failed`, `crashed`,
`timeout`), `stage` (`start`, `load`, `descriptor`, `symbols`, `unload`, `exit`,
`capture`), целые либо null `exitCode`/`signal`, `capture`, `diagnostics`.
`completedStages` перечисляет по порядку успешно завершённые `load`, `descriptor`,
`symbols`, `unload`; это ещё не означает успешное завершение процесса.
Успех требует явной выгрузки, достоверного служебного результата и нормального
завершения до исходного дедлайна. Выбранный вариант получает не более одной
попытки на уникальный модуль.

`capture.stdout` и `capture.stderr` содержат `dataBase64` (стандартный Base64
с padding без переносов) и логическое `truncated`. Независимо сохраняются
первые 65536 байт каждого потока; ровно 65536 без продолжения не усечены.
Произвольные байты точно восстанавливаются декодированием. Очистка содержит
`state` (`passed`/`failed`), собственные `diagnostics` и `remainingPaths`;
её ошибка не затирает исход пробы. При ошибке необязательный `temporaryDirectory`
указывает известный абсолютный собственный каталог; если путь неизвестен,
поле отсутствует. При неуспешной очистке `remainingPaths: []` не утверждает,
что каталог отсутствует: текущее существование не выдумывается без доказательств.
После отдельного 5-секундного ожидания клиента служба ещё может повторять удаление
в пределах 30-секундного срока; немедленное ручное удаление небезопасно.
См. [границы изоляции процесса и контракт очистки](module-inspect-api.md).

### Диагностики и порядок

Обязательны `severity` (`error`, `warning`, `skipped`), непустые стабильные
`code` и человеческие `message`, массив `path`. Сообщения могут переводиться,
скрипты сопоставляют `code`. `skipped` — невыполненная предпосылка, не ошибка
выполненной проверки и не успех. Архивная диагностика `CHECK_SKIPPED` содержит
`prerequisite` с отдельными `check` и `containerCheck`. Диагностика графа
`REFERENCE_SKIPPED` использует `check: "references"` и путь зависимости `path`,
но не архивный объект `prerequisite`. Проверки: `input`, `container`, `bytecode`,
`native`, `references`; архивные предпосылки используют первые четыре имени.
Контейнерные этапы: `layout`, `content`, `json`, `declarations`, `native_hash`,
`main_hash`.

Необязательны `check`, `archive`, `entryBase64`, `field`, `index`, `reference`,
`systemError`, `prerequisite`, `expectedHash`, `actualHash`. `entryBase64`
сохраняет точные байты имени ZIP-записи даже при неверном UTF-8. JSON-указатели
и ссылки сохраняют строки с экранированными управляющими символами/NUL.
Шаг `path` содержит `name`, `version`, `expectedHash` (каждое строка либо null),
не использует нестабильные индексы обнаружения. Для корневой диагностики путь
пуст. Хеши выводятся полностью.

Модули имеют тот же порядок name/version/archive, что и текст. Общий API графа
задаёт детерминированный порядок диагностик и все значимые входящие цепочки,
но каждый уникальный модуль проверяется один раз. Отдельного валидатора или
повторного анализа нет. Ошибочный JSON не вызывает каскад ошибок полей и хеша
выдуманного объекта. Зависимые проверки явно пропускаются.

### Стабильные коды

| Code | Значение |
| --- | --- |
| `INVALID_ARGUMENT` | Неверные параметры команды. |
| `TARGET_IO` | Ошибка чтения профиля цели. |
| `TARGET_INVALID` | Неверный синтаксис, структура или смысл профиля. |
| `ARCHIVE_IO` | Ошибка файлового доступа к архиву или путям поиска. |
| `RESOURCE_LIMIT` | Достигнут заданный предел или предел адресного пространства. |
| `OUT_OF_MEMORY` | Обработанная ошибка выделения памяти. |
| `ZIP_INVALID` | Ошибка структуры, сжатия или целостности ZIP. |
| `CONTAINER_INVALID` | Ошибка состава, путей, типов или объявлений записей архива. |
| `JSON_INVALID` | Ошибка JSON, типов, схемы или нормализации метаданных. |
| `MAIN_HASH_MISMATCH` | Основной хеш отличается от пересчитанного. |
| `NATIVE_HASH_MISMATCH` | Native-данные отличаются от заявленного хеша. |
| `BYTECODE_INVALID` | Неверный байткод или его связи с метаданными. |
| `NATIVE_INVALID` | Неверные native-метаданные или внутренняя согласованность. |
| `CHECK_SKIPPED` | Проверка не выполнена из-за неуспешной предпосылки. |
| `DEPENDENCY_NOT_FOUND` | Требуемый модуль точной версии не найден. |
| `MODULE_INVALID` | Выбранная зависимость повреждена; другая копия не подставляется. |
| `IDENTITY_MISMATCH` | Имя/версия не соответствуют запросу. |
| `DEPENDENCY_HASH_MISMATCH` | Хеш зависимости не соответствует ожидаемому. |
| `DEPENDENCY_HASH_CONFLICT` | Разные входящие запросы закрепили разные хеши одной идентичности. |
| `DEPENDENCY_CYCLE` | Найден цикл зависимостей. |
| `REFERENCE_INVALID` | Внешняя ссылка на функцию не прошла проверку графа. |
| `REFERENCE_SKIPPED` | Проверка ссылки зависит от неразрешённого ребра графа. |
| `NO_NATIVE_VARIANT` | Нет подходящего native-варианта; VM остаётся возможной. |
| `AMBIGUOUS_NATIVE_VARIANT` | Подходит несколько вариантов; произвольного выбора нет. |
| `REPORT_FAILED` | Обработанная ошибка построения отчёта/инфраструктуры. |
| `PROBE_ABI_INVALID` | Descriptor или объявленный символ нарушает native ABI (ошибка). |
| `PROBE_INFRASTRUCTURE_FAILED` | Ошибка запуска, служебного протокола или захвата пробы (ошибка). |
| `PROBE_CRASHED` | Аварийное завершение процесса пробы (предупреждение). |
| `PROBE_TIMEOUT` | Проба не завершилась до исходного дедлайна (предупреждение). |
| `PROBE_LOAD_FAILED` | Загрузчик ОС не смог загрузить выбранную библиотеку (предупреждение). |
| `PROBE_UNLOAD_FAILED` | Явная выгрузка библиотеки не удалась (предупреждение). |
| `PROBE_FAILED` | Иное неуспешное завершение пробы (предупреждение). |
| `CLEANUP_TIMEOUT` | Истекло ожидание результата очистки; служба ещё может работать (ошибка). |
| `CLEANUP_FAILED` | Не удалось очистить собственное дерево процессов или временные данные (ошибка). |

Непустые code и message обязательны; errno и текст загрузчика не используются
как code.
Неизвестный будущий код не отменяет явно указанную severity/невыполненность.
