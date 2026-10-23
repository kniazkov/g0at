# Канонический JSON / Canonical JSON (#180)

## Русский

`src/module/module_json.h` предоставляет два уровня записи:

- `goat_json_canonical(value, &bytes, &length)` сериализует разобранное JSON-значение.
  Сортирует ключи объектов, сохраняет порядок всех массивов и не знает схему модуля.
- `goat_module_json_write(doc, kind, main_metadata, &bytes, &length)` проверяет
  схему, нормализует описание и записывает канонические байты. При `main_metadata`
  допускается только `GOAT_MODULE_METADATA`; остаются ровно поля основного хеша:
  `formatVersion`, `bytecodeVersion`, `name`, `version`, `main`, `files`,
  `dependencies`, `functions`, `externals` (отсутствующие необязательные опущены).

Вход заимствован и не меняется. Выход принадлежит вызывающему; освобождение —
`goat_module_json_free(bytes)`. Длина не включает завершающий служебный NUL.
При ошибке указатель обнуляется, длина равна нулю: частичный результат не выдаётся.
Память и переполнение размера проверяются, глубина ограничена теми же 256 уровнями,
что у читателя. Функции не имеют изменяемого глобального состояния и не зависят
от локали. SHA-256 и сверка с байткодом относятся к последующим задачам.

Ключи сортируются по **декодированным беззнаковым байтам UTF-8**, включая встроенный
NUL; при общем префиксе короткий ключ первый. Повтор ключа запрещён. Выход не
содержит BOM, пробелов форматирования или завершающего перевода строки. `"` и `\`
используют двухбайтовые escape; U+0000–001F всегда записываются как строчные
`\u00xx`, включая `\u000a` вместо `\n`. Остальные скаляры, включая `/`, передаются
непосредственно в UTF-8. Непарные суррогаты и некорректный UTF-8 отвергаются даже
в программно созданном дереве. Unicode-нормализации нет.

Числа — точные `int64`, в том числе `9007199254740993`, `-9223372036854775808`
и `9223372036854775807`. RAW-токены проверяются; дроби, экспоненты, начальный плюс,
лишние нули и выход за диапазон запрещены. `-0` записывается как `0`.
Вещественные значения JSON запрещены. Константы Goat `real` — **строки** из 16
строчных hex-цифр; writer сохраняет биты +0/-0, субнормальных чисел, бесконечностей
и всех NaN без преобразования в double. `character` содержит ровно один Unicode-
скаляр, включая U+0000 и символы вне BMP. Пустая строковая константа и false
сохраняются; информационные пустые поля нормализуются отдельно.

`goat_module_json_read` теперь выполняет ту же нормализацию, что писатель:

1. Проверить всю исходную схему и версии. Затем проверить значения скаляров,
   ограничения фактов, число позиционных аргументов и локальные ссылки. Ошибочные
   данные не становятся допустимыми из-за последующего удаления.
2. В фактах удалить избыточное `value:null`, `nonNull:true` при известном ненулевом
   типе, полный int64-диапазон и согласованный диапазон рядом с константой.
   Одноточечный диапазон заменить точной константой. Перевёрнутый диапазон и
   константа вне диапазона — ошибки. Правила сокращения извлечены из существующего
   `make_integer_range_element` в общий `goat_classify_integer_range`: решётка и
   JSON используют один код, без зависимости автономной модульной библиотеки от VM.
3. Рекурсивно проверить и нормализовать `elements`; удалить пустое описание либо
   ограничения элементов доказанно пустого массива. `type:array` и `empty:true`
   сохраняются. Даже удаляемое `elements` не может скрывать ошибку.
4. Удалить пустой `result` и полностью неизвестные эффекты (`may:[], unknown:true`).
   Чистота (`unknown:false`) сохраняется. Категории эффектов сортируются как
   `externalRead, externalWrite, input, output`; повтор — ошибка.
5. Только после проверки арности удалить целиком `when.arguments`, если все
   элементы пусты. Иначе сохранить каждый элемент и позицию. Проверить запрет
   любого `result` вместе с `normalReturn:false` до удаления пустого результата.
6. Сравнить **все нормализованные сводки** по полным каноническим байтам; повтор —
   ошибка, а не дедупликация. Затем удалить сводки без утверждений и пустой
   `analysis`. Содержательные сводки сортируются по каноническим байтам.
7. Отсортировать функции численно по ID, зависимости по имени/версии, производителей
   по имени/версии/buildId. Дубликаты запрещены. Сохранить имена экспортов и mutable,
   даже если других фактов нет; проверить собственные functionId и запрет гарантий
   для изменяемого экспорта. Позиционные `files`, `parameters` и порядок `authors`
   не меняются.
8. Нормализовать версии цели (удалить завершающие нули, оставить хотя бы один),
   отсортировать CPU features, условия guard по каноническим байтам и специализации
   численно. Удалить пустые conditions. Варианты сортируются по цели/ABI/хешу,
   затем имени библиотеки при полном равенстве этих ключей. Последний критерий
   влияет только на запись, не на выбор платформы.

Нормализация идемпотентна. Она не доказывает истинность аналитических утверждений,
не проверяет перекрытие нативных guard, граф зависимостей, байткод или ABI бинарной
библиотеки: эти проверки выполняются соответствующими последующими компонентами.

Исправлены два расхождения старого Python-эталона с задачами: лексикографический
порядок эффектов и удаление пустого authors в `.gmod`. Уточнён последний критерий
для равных ключей нативных вариантов. Существующие эталонные бинарные файлы и хеши
не изменились. В `scripts/check_module_canonical.py` проверяются точные байты,
эквивалентные и различающиеся формы, идемпотентность, ошибки до нормализации,
дубликаты после нормализации и проекция метаданных; запуск — с путём к
`module_json_testing`. Проверки включены в Linux/Windows CI.

## English

`src/module/module_json.h` exposes two levels:

- `goat_json_canonical(value, &bytes, &length)` serializes parsed JSON, sorting
  object keys while preserving every array's order; it has no module schema.
- `goat_module_json_write(doc, kind, main_metadata, &bytes, &length)` validates
  and normalizes a descriptor before canonical encoding. Main-metadata mode is
  available only for metadata and retains exactly formatVersion, bytecodeVersion,
  name, version, main, files, dependencies, functions and externals, omitting
  absent optional fields.

Inputs are borrowed and unchanged. Release owned output using
`goat_module_json_free`. Length excludes a convenience trailing NUL; failures
return NULL/zero with no partial output. Size arithmetic and allocations are
checked; the reader's 256-level nesting limit also applies. There is no mutable
global state or locale dependency. SHA-256 and binary consistency are later tasks.

Keys use decoded unsigned UTF-8 order, including embedded NUL, with shorter
prefixes first. Duplicates are errors. Output has no BOM, formatting whitespace
or final newline. Quote/backslash use two-byte escapes; every U+0000–001F uses
lowercase `\u00xx` (including `\u000a`, not `\n`). All other scalars, including
slash, remain direct UTF-8. Invalid UTF-8 and surrogates are rejected even in
programmatically constructed trees. No Unicode normalization occurs.

Integers retain exact int64 values, including 9007199254740993 and both endpoints.
RAW tokens must be integer decimals: no fractions, exponents, leading plus,
redundant zeros or overflow. Negative zero becomes zero. JSON reals are rejected.
Goat real constants use 16 lowercase hex digits in strings, retaining signed
zeros, subnormals, infinities and NaN bits without floating-point conversion.
Characters contain exactly one Unicode scalar, including NUL or supplementary
characters. Empty string constants and false remain known values.

The reader and writer share normalization after complete schema validation:

1. Check scalar values, facts, argument arity and local references before dropping
   information. Remove redundant null values/nonNull facts. Reduce singleton and
   full int64 ranges and ranges implied by constants, rejecting reversed bounds or
   inconsistent constants first. The existing lattice constructor's reductions
   were extracted into `goat_classify_integer_range`, shared by the constructor
   and JSON without coupling the module library to the VM.
2. Recursively validate elements even when empty:true will make them redundant;
   remove empty element descriptions or elements of a proven-empty array.
3. Remove empty result and fully unknown effects; preserve proven purity. Sort
   effects as externalRead, externalWrite, input, output and reject duplicates.
   Reject any result alongside normalReturn:false before normalization.
4. Check argument arity, then omit the entire arguments array only if all entries
   normalize to empty facts. Otherwise retain every position.
5. Reject duplicate normalized summaries by full canonical bytes **before**
   removing summaries with no claims. Sort retained summaries by those bytes;
   omit empty analysis.
6. Sort functions numerically, dependencies by name/version, producers by
   name/version/buildId, rejecting duplicates. Retain named exports and their
   mutable flag; validate own function bindings and reject guarantees on mutable
   exports. Preserve files, parameters and authors order.
7. Trim target version trailing zeros while retaining one component; sort unique
   CPU features, canonical guard predicates and numeric specialization IDs.
   Omit empty conditions. Sort variants by canonical target, ABI, hash and, for
   equal keys, library name as a serialization-only tie-break.

Normalization is idempotent. It does not re-prove analysis, detect native guard
intersection, validate dependency graphs, bytecode or library ABI; those remain
with their later validators.

The old Python reference's effect order and empty `.gmod` authors handling were
corrected to match the accepted tasks. Equal variant keys now have a deterministic
final tie-break. Existing binary/hash vectors are unchanged. Run
`python scripts/check_module_canonical.py <module_json_testing>` for exact bytes,
equivalence/difference, idempotence, pre-normalization errors, post-normalization
duplicates and main-metadata projection. These tests run in Linux/Windows CI.
