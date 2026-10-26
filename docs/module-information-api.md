# Module information / Информационные данные модуля

`goat_module_information(descriptor, kind, out)` in `src/module/module_json.h`
returns an owned information-only yyjson document. Acceptable kinds are `.gmod`
and `module.json`; the entire descriptor is validated and normalized first through
the existing reader/writer, including non-informational fields. This does not
validate bytecode or the dependency graph; use the shared module-validation API
for that. Inputs are borrowed and unchanged. Output strings are independent of
the input lifetime. Failure clears `*out`; success with no information is `{}`.
Free the result with `yyjson_doc_free`. Canonical output can use the existing
`goat_json_canonical` function. Do not feed this projection into the main hash or
interpret it as a complete module descriptor.

The allowlist is description, license, homepage, authors and module-only producers.
Only empty optional strings/lists are removed; whitespace, case, Unicode composition,
line breaks and embedded NUL are preserved. Consumers must use explicit string
lengths, including when displaying names or comparing records. Authors are unique
by exact name/email after empty-email removal and retain their specified order.
Producer records are unique exact name/version/buildId triples, sorted by unsigned
UTF-8 component bytes, not by version precedence. Producer strings are diagnostic
text, not module names/versions. Arbitrary email, homepage and license text is
accepted: no address/URL/SPDX parsing or network lookup takes place.

All informational fields are excluded from main SHA-256. Invalid structures still
fail before projection or hashing. Repackaging does not generate new producer IDs,
change author order or add the packager as a producer. Compiler generation (#191),
CLI display (#212) and the merge policy below consume this projection.
The projection itself makes no merge choice for the user.

The existing `module_json_testing` harness exercises this API; no extra executable
or alternate schema is introduced. `scripts/check_module_information.py` checks
all fields/types, decoded escapes, UTF-8/NUL, whitespace and normalization, author
identity/order, producer structure/order, exact textual distinctions, hash invariance,
and full writer/repack round trips. It inspects the standalone production-linked
harness symbols for network-client imports on every CI platform. The implementation
uses only in-memory JSON/allocation operations, with no system loader or network call.

## Русский контракт

Функция возвращает отдельный владеющий JSON-объект после проверки полного `.gmod`
или `module.json`; исходный документ не меняется. Это не проверка байткода/графа
зависимостей: для них нужен единый API проверки модуля. Пустой успешный результат
равен `{}`, ошибка обнуляет указатель. Освобождение — `yyjson_doc_free`, канонизация —
`goat_json_canonical`. Информационную проекцию нельзя использовать как основной хеш
или полный дескриптор модуля.

Сохраняются только description/license/homepage/authors и producers для module.json.
Пустые необязательные строки/массивы удаляются; пробелы, регистр, Unicode-композиция,
переводы строк и U+0000 сохраняются. Используйте длины строк, а не strlen.
Авторы уникальны по точным name/email после удаления пустого email, порядок значим.
Производители уникальны по name/version/buildId и сортируются по беззнаковым UTF-8
байтам, не по старшинству версий. Это диагностические строки, не идентичности модулей.
Email, homepage и license — произвольный текст; сетевых запросов и проверок URL/SPDX нет.

Информационные поля исключаются из SHA-256, но не из проверки корректности структуры.
Перепаковка сохраняет происхождение и порядок авторов. Генерация компилятором (#191), CLI (#212) и описанная ниже политика объединения
используют эту проекцию; сама проекция не выбирает порядок авторов.
Тесты покрывают все поля и неверные типы, точное сохранение и экранирование, дубликаты,
порядок производителей, неизменность хеша и полный цикл записи/перепаковки. Проверяется
отсутствие импортов сетевых API в исполняемом тестовом файле на каждой платформе CI.

## Merge policy (#222)

`src/module/module_information_merge.h` implements the information-policy stage,
not the `goat-merge` archive CLI (#215). `goat_information_merge_prepare` receives
all complete `module.json` descriptors together. The caller first removes repeated
**exact source argument strings**; the policy does not deduplicate documents,
paths, physical files, main hashes or identical author lists. Each descriptor is
validated with the existing schema/normalizer before comparing information.
Invalid technical fields and duplicate authors/producers cannot bypass validation.
The plan owns independent strings and does not modify its inputs.

Description, license and homepage must match exactly after decoding/empty-value
normalization, including presence: a nonempty value in only one source conflicts.
No trimming, case conversion, Unicode normalization or C-string truncation occurs.
Producers form a deduplicated union sorted by unsigned UTF-8 component bytes;
producer identity is the exact name/version/buildId triple. No packager identity
is introduced. These fields remain outside the main hash.

Zero nonempty author lists produce no authors. One keeps its original order.
Two or more require explicit selection even for identical lists, with all sources
counted simultaneously. Candidate numbers are stable for one plan; their display
order is not an automatically accepted result order. The union contains each exact
normalized name/email pair once. `goat_information_merge_finish` accepts a complete
zero-based permutation only when selection is required, otherwise no order. It
returns an owned information-only document (free with `yyjson_doc_free`) and clears
the output on failure. Free plans with `goat_information_merge_free`.

The production `goat_information_merge_interactive` adapter displays numbered
candidates with JSON-escaped names/emails (including embedded NUL), accepts one
line of one-based decimal numbers, and diagnoses invalid answers before retrying
with unchanged numbering. Empty, incomplete, repeated, nonnumeric, overflowing
and out-of-range selections fail. EOF or stream failure produces no result.
The caller supplies terminal eligibility explicitly: unavailable interaction
fails immediately when selection is required, without reading input; zero/one
nonempty lists need no interaction. Streams are borrowed and never closed.
`goat_information_merge_parse_order` also exposes the same length-aware parser.

This stage performs no archive I/O, native loading, bytecode execution, dependency
lookup or publication. #215 is responsible for source/path checks, main-hash and
variant compatibility, validated deterministic archive writing and atomic
publication only after successful selection; #224 adds final autonomous-merge
coverage. Existing destination files therefore cannot be changed by this API.
The existing `module_json_testing` executable and
`scripts/check_module_information_merge.py` exercise the production policy and
interactive adapter in Linux and Windows CI, without introducing another schema
or executable.

## Политика объединения (#222)

`src/module/module_information_merge.h` реализует этап обработки информационных
данных, а не архивную CLI `goat-merge` (#215). `goat_information_merge_prepare`
получает все полные дескрипторы `module.json` одновременно. Вызывающая сторона
сначала удаляет повторы **точных строк аргументов source**. API не объединяет
документы, пути, физические файлы, основные хеши или одинаковые списки авторов.
Каждый дескриптор проходит существующую проверку и нормализацию до сравнения;
некорректные технические поля и повторы авторов/производителей не обходят проверку.
План владеет независимыми строками и не меняет входные документы.

Description, license и homepage сравниваются точно после декодирования и удаления
пустых значений. Непустое поле только в одном источнике — конфликт. Пробелы,
регистр, Unicode-композиция и U+0000 сохраняются. Producers объединяются по точной
тройке name/version/buildId и сортируются по беззнаковым UTF-8 байтам компонентов.
Упаковщик не добавляется; основной хеш не меняется.

При отсутствии непустых списков authors поле отсутствует. Единственный непустой
список сохраняет порядок. Два и более всегда требуют явного выбора, даже если
списки одинаковы; учитываются сразу все входы. Кандидаты уникальны по нормализованной
паре name/email; их нумерация постоянна в рамках плана и не задаёт итоговый порядок.
`goat_information_merge_finish` принимает полную перестановку индексов от нуля
только при необходимом выборе, иначе порядок не передаётся. Результат — владеющая
информационная проекция (`yyjson_doc_free`), ошибка обнуляет выход. План освобождается
через `goat_information_merge_free`.

Рабочий адаптер `goat_information_merge_interactive` показывает имена/email как
JSON-строки (включая U+0000), читает одну строку номеров от единицы, объясняет ошибку
и повторяет запрос с прежней нумерацией. Пустой ответ, пропуск, повтор, нечисловое
значение, переполнение и неверный номер отвергаются. Конец ввода и ошибка потока
не создают результата. Доступность интерактивного ввода передаёт вызывающая сторона:
при обязательном выборе его отсутствие сразу даёт ошибку без чтения; для нуля/одного
списка ввод не нужен. Потоки не закрываются. Тот же разбор с явной длиной доступен
через `goat_information_merge_parse_order`.

API не читает архивы, не загружает библиотеки, не исполняет байткод, не ищет
зависимости и не публикует результат. Проверки путей, основного хеша и вариантов,
детерминированная запись, проверка временного архива и атомарная публикация после
выбора относятся к #215; итоговая автономность — к #224. Существующее назначение
этот API изменить не может. Текущий `module_json_testing` и
`scripts/check_module_information_merge.py` проверяют рабочую политику и адаптер
в Linux/Windows CI без отдельной схемы или исполняемого файла.
