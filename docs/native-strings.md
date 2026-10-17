# Native string declarations / Объявления нативных строк

## Русский

Публичный `goat/native_abi.h` объявляет два типа:

| Тип | Поля | Назначение |
| --- | --- | --- |
| `goat_native_string_view_v2_t` | `const wchar_t *data`, `size_t length` | Неизменяемое представление чужой памяти, в том числе подстроки |
| `goat_native_string_buffer_v2_t` | `wchar_t *data`, `size_t length`, `size_t capacity` | Изменяемый буфер, выделенный через контекст нативного вызова |

Длина и вместимость измеряются в единицах `wchar_t`, как в текущей VM,
а не в байтах или видимых символах. На Windows символ вне BMP занимает две
UTF-16 единицы; на обычном Linux — одну 32-битную единицу. Представление
`wchar_t` у интерпретатора и модуля должно совпадать. Это внутрипроцессные
структуры, а не переносимый формат сериализации. Они сохраняют единицы VM
без нормализации или проверки Unicode.

Представление не требует завершающего нуля и никогда не освобождает память.
`{NULL, 0}` — допустимая пустая строка. При ненулевой длине указатель обязан
давать доступ к указанному числу единиц. Встроенные нули допустимы: длину
нельзя вычислять через `wcslen`.

Буфер `{NULL, 0, 0}` не требует выделения памяти. Непустое выделение содержит
`capacity + 1` единиц, `length <= capacity`, `data[length] == 0`. Перед
выделением необходимо проверить переполнение сложения и умножения на
`sizeof(wchar_t)`. Вместимость не включает завершающий ноль. Даже пустой
буфер может сохранять выделенную память.

Память выделяется только через `goat_native_allocate` и освобождается через
тот же [контекст](native-memory.md). Индивидуально освобождать можно только
исходный указатель выделения, не подстроку; после освобождения поля буфера
обнуляются. Копирование структуры не создаёт второго владельца. Замена или
освобождение памяти инвалидирует представления на неё. Буфер и его
представления не переживают внешний нативный вызов; интерпретатор обязан
скопировать результат до общей очистки контекста.

Операции объявлены в `goat/native_strings.h`, который также подключается из
`native_runtime.h`. Реализации находятся в `src/native/strings.c` и собираются
в `libgoat_native.a`; [правила линковки](native-runtime.md). Все функции имеют префикс
`goat_native_string_`:

| API | Метод языка |
| --- | --- |
| `length`, `is_empty`, `is_blank` | `length`, `isEmpty()`, `isBlank()` |
| `char_at`, `substring`, `trim` | `charAt(i)`, `substring(begin, end)`, `trim()` |
| `contains`, `index_of`, `last_index_of` | `contains(s)`, `indexOf(s)`, `lastIndexOf(s)` |
| `starts_with`, `ends_with` | `startsWith(s)`, `endsWith(s)` |
| `to_upper_case`, `to_lower_case` | `toUpperCase()`, `toLowerCase()` |
| `replace`, `repeat`, `format` | `replace(old, new)`, `repeat(n)`, `format(...)` |
| `concat` | конкатенация `+` |
| `copy`, `view`, `release` | вспомогательные операции владения |

Методы, создающие строку, принимают контекст и указатель на **пустой выходной
буфер**, возвращают статус `GOAT_NATIVE_OK`, `GOAT_NATIVE_BAD_REQUEST` или
`GOAT_NATIVE_ABI_MISMATCH`. При ошибке выходной параметр не меняется. Результат
всегда независим от входной памяти; после использования его можно освободить
через `release` или оставить общей очистке. Нельзя передавать непустой выходной
буфер или совмещать его с входом. `char_at` возвращает статус и пишет код в
`uint32_t *`; отрицательный индекс или индекс за границей — ошибка. Для
`substring(begin)` передай длину как `end`. `is_blank` также возвращает статус
и пишет результат в `bool *`. Остальные предикаты и поиск возвращают значение
напрямую; поиск несуществующей подстроки возвращает `-1`.

Входные представления должны удовлетворять контракту и иметь длину не больше
`GOAT_NATIVE_STRING_MAX`. Библиотека проверяет арифметику размера результата;
отрицательные повторы и непредставимый размер возвращают `BAD_REQUEST`.
Реальное исчерпание памяти остаётся фатальным в аллокаторе интерпретатора.
Ни `malloc`, ни `free`, ни скрытых глобальных буферов в нативной библиотеке нет.

`format` принимает массив **уже преобразованных в строки** аргументов.
`%0`, `%1` и т. д. подставляются без повторного разбора подставленного текста;
отсутствующие и переполненные номера остаются как есть. Символьные аргументы
поиска, замены и конкатенации также передаются как строковые представления.
Преобразование произвольных объектов языка — обязанность вызывающего адаптера.

Для `trim`, `is_blank` и регистра используются три дополнительных сервиса хоста
в конце таблицы: `string_is_space`, `string_to_upper`, `string_to_lower`.
Они работают в CRT интерпретатора с его инициализированной `LC_CTYPE`, поэтому
статически слинкованная Windows DLL не получает другую локаль. Операции не
меняют локаль. Размер префикса проверяется перед доступом; старый хост только
с памятью возвращает `ABI_MISMATCH` для этих операций. Числовые модули сохраняют
совместимость. Хост обязан инициализировать локаль до конкурентных вызовов.

Интеграция строк в генерируемые адаптеры и `goat_native_value_v2_t` остаётся
отдельным этапом: этот слой уже выполняет операции в C/DLL/SO, но сам по себе
не переводит строковые функции языка из VM в нативный режим.

## English

`goat/native_abi.h` declares an immutable borrowed `goat_native_string_view_v2_t`
(data, length) and a mutable context-owned `goat_native_string_buffer_v2_t`
(data, length, capacity). Length and capacity count host `wchar_t` code units,
matching the VM; they do not count bytes or Unicode characters. Host and module
must agree on the wchar_t representation. These are in-process declarations,
not a portable serialization format; no Unicode validation or normalization is implied.

A view needs no terminator, permits embedded zeros, and never releases storage.
NULL data is permitted only for zero length. A buffer starts as `{NULL, 0, 0}`;
allocated storage contains capacity + 1 units, length <= capacity, and a zero
at data[length]. Check addition and multiplication overflow before allocation.
Capacity excludes the terminator; length is authoritative even with embedded zeros.

Allocate and release through the same native memory context. Only the original
allocation pointer may be released; reset the buffer afterwards. Copying the
struct does not duplicate ownership. Replacing/releasing storage invalidates
borrowed views. Neither type extends storage lifetime beyond the outer call;
the host must consume/copy results before bulk cleanup.

`goat/native_strings.h` declares all string methods listed above plus concatenation,
and is included by `native_runtime.h`. Implementations are compiled separately from
`src/native/strings.c` into `libgoat_native.a`; see [linking instructions](native-runtime.md). Producers take a context and an **empty** output
buffer, return an ABI status, and leave output unchanged on failure. Outputs own fresh
context storage; release them explicitly or leave them to outer-call cleanup. Inputs
must be valid views no longer than `GOAT_NATIVE_STRING_MAX`; input/output aliasing is
not supported. Result-size arithmetic is checked before allocation. Negative repeats
and unrepresentable sizes return BAD_REQUEST; actual allocation failure is host-fatal.
There are no module-owned heap allocations or mutable global buffers.

`char_at` writes a uint32_t code unit with bounds checking; `substring` requires both
indices (use length for an omitted end). `is_blank` writes a bool and returns status.
Other predicates/searches return values directly, with -1 for a missing substring.
`format` takes already stringified argument views, substitutes decimal %indices once,
and preserves missing/overflowing placeholders. Character arguments to searches and
replacement likewise use converted views; VM object conversion belongs to the caller.

Locale-dependent operations call optional host services appended after the memory
prefix. This preserves the host CRT/LC_CTYPE even with separate Windows CRTs. The host
initializes its locale before concurrent calls; native operations never change it.
A host with only memory services remains valid for numeric modules and returns
ABI_MISMATCH for the new locale-dependent operations. Existing value layout is unchanged.

Adapter generation and string transport in `goat_native_value_v2_t` are a separate
step: these C/DLL/SO operations do not yet enable native dispatch of Goat string functions.

The differential test builds an independent shared module, invokes it through host
callbacks, compares all methods with unoptimized VM execution, and checks empty/embedded
zero strings, Unicode, bounds, overflow, missing services and memory cleanup.
