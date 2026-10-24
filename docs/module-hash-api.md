# Основной SHA-256 модуля / Module main SHA-256

## Русский

`src/module/module_hash.h` предоставляет независимый от VM API библиотеки
`goat_module`: `goat_module_main_hash` вычисляет хеш, а
`goat_module_main_hash_verify` сравнивает его с `mainHash` метаданных.
Обе функции получают заимствованный `yyjson_doc`, указатель на точные байты
байткода и их длину. Вход не изменяется. NULL вместо байткода допустим только
при нулевой длине. Вычисление записывает 64 строчные hex-цифры и NUL в буфер
из `GOAT_MODULE_HASH_HEX_SIZE` (65) байтов; при ошибке весь буфер обнуляется.
Проверка возвращает `GOAT_MODULE_JSON_OK` и `matches=false` при несовпадении;
ошибки входа возвращают соответствующий JSON-код и также сбрасывают matches.

Сначала проверяется и нормализуется весь документ по контракту #178/#180,
включая поля, исключённые из хеша. Обязательный mainHash должен содержать
64 строчные hex-цифры; при создании модуля можно передать 64 нуля.
Хешируемая проекция содержит ровно formatVersion, bytecodeVersion, name,
version, main, files, dependencies, functions, externals. Отсутствующие поля
опускаются. mainHash, native и информационные поля не участвуют.

Поток SHA-256: 18 байтов `GOAT-MODULE-MAIN\x00\x01`, длина канонического
JSON как uint64 little-endian, канонические байты JSON, длина байткода как
uint64 little-endian, точные байты байткода. Без терминаторов и разделителей
сверх указанных. Используется существующая реализация SHA-256, расширенная
потоковым API; дополнительная память пропорциональна метаданным, байткод
не копируется. Суммарный вход ограничен `UINT64_MAX / 8` байтами из-за
64-битной битовой длины SHA-256; переполнение возвращает JSON_LIMIT до чтения
байткода. Состояние локальное, глобальных изменяемых данных нет.

Успешная проверка означает совпадение идентичности сборки. Она не проверяет
структуру байткода, согласованность import и dependencies, граф зависимостей,
ZIP или нативный ABI; эти проверки выполняют последующие этапы загрузчика.

`python scripts/check_module_hash.py <module_json_testing>` сверяет C API
с фиксированными векторами и независимым hashlib через reference.py,
проверяет границы блоков, изменения данных, нормализацию и ошибки.
Тест запускается в Linux/Windows CI и Linux-сборке с заполнением стека pattern.

## English

`src/module/module_hash.h` exposes the VM-independent `goat_module` API:
`goat_module_main_hash` computes the digest; `goat_module_main_hash_verify`
compares it with metadata.mainHash. Both borrow an immutable yyjson document
and the exact bytecode buffer and length. NULL bytecode requires zero length.
The output buffer has `GOAT_MODULE_HASH_HEX_SIZE` (65) bytes: 64 lowercase hex
digits plus NUL. Errors clear the entire buffer. Verification returns JSON_OK
with matches=false for a digest mismatch; invalid input returns a JSON error
and clears matches as well.

The complete document is validated and normalized by the #178/#180 contract,
including excluded fields. Required mainHash must be valid lowercase hex;
use 64 zeros while constructing a new module. The projection includes exactly
formatVersion, bytecodeVersion, name, version, main, files, dependencies,
functions and externals, omitting absent fields. mainHash, native and all
informational fields are excluded.

SHA-256 input: 18 bytes `GOAT-MODULE-MAIN\x00\x01`, uint64-LE canonical JSON
length, canonical JSON bytes, uint64-LE bytecode length, exact bytecode bytes.
There are no other terminators or separators. The existing SHA implementation
now supports incremental updates; auxiliary memory scales with metadata,
not bytecode size. Total input is limited to `UINT64_MAX / 8` bytes by the
SHA-256 bit counter; overflow returns JSON_LIMIT before reading bytecode.
All state is local, with no mutable globals.

Successful verification establishes matching build identity. It does not
validate bytecode structure, import/dependency consistency, dependency graphs,
ZIP or native ABI; subsequent loader stages provide those checks.

Run `python scripts/check_module_hash.py <module_json_testing>` for frozen
vectors, independent hashlib/reference comparisons, SHA block boundaries,
normalization, meaningful mutations and invalid input. CI runs it on Linux
and Windows, including Linux builds with patterned automatic variables.
