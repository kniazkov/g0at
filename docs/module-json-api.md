# Строгий JSON описаний модулей / Strict module descriptor JSON

## Русский

`src/module/module_json.h`: `goat_module_json_read(data, length, kind, &doc)`
читает `.gmod` (`GOAT_MODULE_GMOD`) или `module.json` (`GOAT_MODULE_METADATA`), а также самостоятельный
профиль `target.schema.json` (`GOAT_MODULE_TARGET`, см. [выбор native](module-native-api.md)).
Длина задаётся в байтах; вход не обязан завершаться NUL и не изменяется.
При успехе вызывающий владеет самостоятельным `yyjson_doc` и освобождает его
через `yyjson_doc_free`. Исходный буфер можно сразу освободить. При ошибке
`doc == NULL`; код ошибки различает синтаксис/UTF-8, повтор ключа, неизвестное
поле, неверное значение, неподдерживаемую версию и превышение глубины.

Принимается только строгий UTF-8 JSON без BOM, комментариев, завершающей запятой,
непарных суррогатов и данных после документа. Декодированные ключи сравниваются
по полной длине, включая U+0000; экранирование не скрывает дубликат. Имена полей
и перечисления также сравниваются по длине. Проверка всей схемы предшествует
нормализации и любым будущим переопределениям CLI. Читатель не запускает
компилятор, не разрешает зависимости и не обращается к сети или файлам.

Таблицы C автоматически получаются из зафиксированной схемы
[`module-format-v1/schema.py`](module-format-v1/schema.py). Они включают все
вложенные записи v1: файлы, зависимости, функции, аналитику, экспорты, нативные
варианты, цели и специализации. Обычная сборка не требует Python.
`python scripts/generate_module_json_schema.py --check` проверяет отсутствие
расхождений; без `--check` команда обновляет таблицы после изменения спецификации.

Числа остаются `YYJSON_TYPE_RAW`: это точные десятичные токены, **не double**.
Нельзя читать их через `yyjson_get_uint`/`yyjson_get_real`; потребитель должен
разбирать токен в нужный целочисленный тип с проверкой границ. Дроби и экспоненты
запрещены, даже `1.0` и `1e0`. `-0` допустим и остаётся исходным токеном до
канонизации. Индексы ограничены `uint32`, идентификаторы функций ненулевые,
целочисленные значения и границы диапазонов — `int64`. Поддерживается версия 1
для `formatVersion`, `bytecodeVersion` и `analysis.formatVersion`; целая другая
версия отличается от ошибки типа. Проверка нативного ABI — отдельный этап.

Дополнительно проверяются наличие главного файла и файлов функций, уникальность
ID функций и внутренних имён файлов, отсутствие NUL в путях, уникальность пары
имя/версия зависимости и пары имя/email автора. Пустой email эквивалентен
отсутствующему уже при проверке повторов. Две разные версии зависимости допустимы.

После проверки удаляются пустые `description`, `license`, `homepage`,
`authors[].email`, пустые `authors` и пустые необязательные коллекции готового
модуля. В `.gmod` добавляются отсутствующие `dependencies: {}` и настройки
`native.mode: "auto"`, `native.target: "host"` независимо друг от друга.
Явный `null` не заменяет отсутствие. Пробелы и непустые строки сохраняются точно,
без изменения регистра и Unicode-нормализации; email/homepage — произвольный
информационный текст. Путь `main` непустой, без NUL, но может состоять из пробелов.

Глубина JSON ограничена 256 уровнями вложения относительно корня (глубина 0),
чтобы не переполнить стек при рекурсивных аналитических фактах. Это ограничение
ресурсов реализации, а не новая версия формата. Проверка дубликатов ключей
использует сортировку, а не квадратичный перебор. Нехватка памяти при копировании
или нормализации возвращает `GOAT_MODULE_JSON_MEMORY`.

Это этап чтения и проверки **схемы**, а не готовый загрузчик `.gbin`: сверка entry
с инструкциями, доказанных фактов, нативных контрактов и графа зависимостей,
хэширование реализуется отдельной задачей. Канонизация и нормализация фактов
реализованы в [#180](module-canonical-json.md). Читатель не обещает,
что документ, прошедший схему, пройдёт эти последующие проверки. Подключение к
сборщику исходников и merge также относится к их задачам.

Проверки: `module_json_testing` (C),
`python scripts/check_module_json.py <путь-к-module_json_testing>` (реальный C API
через stdin), `python scripts/generate_module_json_schema.py --check`.
В CI проверки выполняются на Linux x86_64/aarch64 с GCC/Clang и Windows
MINGW32/MINGW64/UCRT64. Проверяются положительные эталоны, обязательность и типы,
вложенные неизвестные поля, Unicode/NUL, точные границы чисел, значения по
умолчанию, нормализация и независимость результата от входного буфера.

## English

`goat_module_json_read(data, length, kind, &doc)` in `src/module/module_json.h`
reads `.gmod` (`GOAT_MODULE_GMOD`) or `module.json` (`GOAT_MODULE_METADATA`), and standalone
`target.schema.json` profiles (`GOAT_MODULE_TARGET`; see [native selection](module-native-api.md)).
The byte buffer need not be NUL-terminated and is never modified. Success returns
an independently owned `yyjson_doc`; release it with `yyjson_doc_free`. The input
may be freed immediately. Failure sets `doc` to NULL. Errors distinguish syntax/
UTF-8, duplicate keys, unknown fields, invalid values, unsupported versions and
the nesting resource limit.

Only strict UTF-8 JSON is accepted: no BOM, comments, trailing commas, unpaired
surrogates or trailing data. Decoded keys, field names and enum values use their
full lengths, including embedded NUL. Escapes cannot hide duplicate keys. The
entire schema is validated before normalization or future CLI overrides. Reading
performs no compilation, dependency resolution, filesystem or network access.

Committed C tables are generated from the frozen
[`module-format-v1/schema.py`](module-format-v1/schema.py), covering every nested
v1 record: files, dependencies, functions, analysis, exports, native variants,
targets and specializations. Normal builds require no Python. Run
`python scripts/generate_module_json_schema.py --check` to detect drift; omit
`--check` to regenerate after a specification change.

Numbers remain `YYJSON_TYPE_RAW` decimal tokens, never doubles. Consumers must
parse these tokens with checked integer conversion, not `yyjson_get_uint` or
`yyjson_get_real`. Fractions/exponents, including `1.0` and `1e0`, are rejected.
`-0` is accepted and retains its spelling until canonicalization. Indices use
uint32, function IDs are nonzero, and integer values/range endpoints use int64.
Version 1 is supported for `formatVersion`, `bytecodeVersion` and
`analysis.formatVersion`; an unsupported integer version differs from a type
error. Native ABI compatibility is checked separately.

Descriptor-local checks additionally cover existing main/function files, unique
function IDs and internal filenames, NUL-free paths, unique dependency name/
version pairs and unique author name/email pairs. Empty email equals absent email
for duplicate detection. Different versions of one dependency are allowed.

After validation, empty description/license/homepage/author email, empty authors
and empty optional metadata collections are removed. Missing `.gmod.dependencies`
becomes `{}`; missing native mode and target independently become `auto` and
`host`. Explicit null never means absence. Whitespace and nonempty text remain
unchanged, with no case conversion or Unicode normalization. Email/homepage are
arbitrary informational text. The main path must be nonempty and NUL-free but
may consist entirely of spaces.

Nesting is limited to 256 levels below the root (depth 0) to bound recursive
analysis/schema walking. This is an implementation resource limit, not a format
version change. Duplicate key detection uses sorting rather than quadratic
search. Allocation failures during copying/normalization report
`GOAT_MODULE_JSON_MEMORY`.

This is schema reading/validation, not a complete `.gbin` loader. Entry/instruction
consistency, analysis claims, native contracts, dependency graphs,
hashing belongs to its own task. Canonicalization and fact normalization are
implemented in [#180](module-canonical-json.md). Passing the
schema does not imply passing those later validators. Source builder and merge
integration likewise belongs to their implementation tasks.

Run `module_json_testing`, `python scripts/check_module_json.py <executable>` and
`python scripts/generate_module_json_schema.py --check`. CI runs the production
reader on Linux x86_64/aarch64 GCC/Clang and Windows MINGW32/MINGW64/UCRT64. Tests
cover valid vectors, required fields/types, nested unknown fields, Unicode/NUL,
exact integer boundaries, defaults, normalization and independent output ownership.
