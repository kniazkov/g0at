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
CLI display (#212), conflict resolution and producer unions (#222) remain separate.
The API provides normalized information but makes no merge choice for the user.

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
Перепаковка сохраняет происхождение и порядок авторов. Генерация компилятором (#191),
CLI (#212), объединение и разрешение конфликтов (#222) остаются отдельными задачами.
Тесты покрывают все поля и неверные типы, точное сохранение и экранирование, дубликаты,
порядок производителей, неизменность хеша и полный цикл записи/перепаковки. Проверяется
отсутствие импортов сетевых API в исполняемом тестовом файле на каждой платформе CI.
