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

Этот шаг вводит только типы и контракт. Операций над строками, строкового
тега в `goat_native_value_v2_t`, передачи строк через адаптеры и преобразований
VM пока нет. Размер существующего значения и версия ABI не меняются.

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

Only declarations are introduced. String operations, a native value tag,
adapter transport and VM conversion are future work. Existing value layout
and ABI version are unchanged.
