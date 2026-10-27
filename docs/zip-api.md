# ZIP в памяти / In-memory ZIP

## Русский

`lib/archive/zip_archive.h` — общий API цели `goat_common`, независимый от VM и её
аллокатора. Использует закреплённую zlib 1.3.2 с classic MiniZip.
[Сравнение, лицензии, происхождение и checksum](../third_party/zlib/README.goat.md).
Сборка CMake полностью локальная; zip/unzip и системная ZIP-библиотека не нужны.

`zip_archive_open(data, size, &archive)` заимствует неизменяемый буфер до
`zip_archive_close`. Перечисление идёт по исходному порядку центрального каталога,
без удаления дублей. `zip_archive_layout` показывает исходный архив, EOCD,
комментарий, фактическое/заявленное положение каталога, префикс и хвост.
`zip_archive_entry` и `zip_archive_local` возвращают отдельные центральные и
локальные заголовки: имена, extra fields и комментарии с длинами, методы, флаги,
даты, CRC, размеры, атрибуты, версии и смещения. Никакие имена не обрезаются до
фиксированного буфера; встроенный NUL сохраняется. Все views заимствованы.

Открытие проверяет границы каталога и сравнивает индекс с upstream reader.
Ошибки ZIP64 и многодискового формата различаются. ZIP64-маркеры EOCD,
центральные sentinels и central extra tag 1 отвергаются явно; локальные extra
fields, включая ZIP64, доступны без удаления через `zip_archive_local`.
Неподдерживаемые методы, шифрование, ссылки в атрибутах, необычные пути,
несовпадение локальных/центральных данных не скрываются. Для повреждённого
локального заголовка accessor возвращает ZIP_FORMAT; центральный остаётся доступен.

`zip_archive_read` пишет в буфер вызывающей стороны не более capacity байтов.
Заявленный размер проверяется до записи; Deflate обрабатывается потоково с
ограниченным выходом. Проверяются завершение потока, полное потребление сжатых
байтов, фактический размер, CRC и совпадение имени, метода, флагов, версии, даты
и времени в заголовках. Без data descriptor также должны совпасть CRC и размеры.
При ошибке `written=0`, но буфер может содержать частичные недоверенные данные.
Буфер вывода не должен пересекаться с неизменяемым архивом. Чтение ничего не
создаёт в файловой системе. Можно читать записи повторно и в любом порядке;
само чтение не изменяет handle.

`zip_archive_write(entries, count, comment, max_size, &data, &size)` создаёт
архив в памяти с ограничением выделения/записи max_size. Выход принадлежит
вызывающему коду: освобождать через `zip_archive_free`. При ошибке выход NULL/0,
все временные ресурсы освобождаются. Все входные buffers заимствуются лишь на
время вызова. Записи идут в переданном порядке; дубли не удаляются. Имена
непустые, без NUL; комментарии произвольные байты; extras — полные ZIP TLV
records без ZIP64. Неверные длины и неподдерживаемые параметры дают ошибки.

Параметры каждой записи: метод 0 или 8, флаг 0 или 0x0800, точные DOS date/time,
made-by, внутренние/внешние атрибуты, отдельные local/central extras и комментарий.
Метод не выбирается эвристически: Deflate используется даже для пустых,
коротких и несжимаемых данных. Фиксировано: zlib 1.3.2, уровень 9,
windowBits=-15, memLevel=8, Z_DEFAULT_STRATEGY, один Z_FINISH без промежуточных
flush; version-needed=20, single disk, без data descriptor и автоматического
ZIP64. Неявные compression/UTF-8 flags и text attribute MiniZip заменяются
точными значениями вызывающей стороны в готовых полях той же длины. Так же
восстанавливаются точные даты и бинарные комментарии. ZIP/Deflate не реализуются
заново. Текущее время и свойства машины в архив не добавляются.

Лимиты: меньше 65535 записей, размер каждого содержимого и всего результата
меньше 0xffffffff; ёмкость также ограничена size_t. Нет сортировки, переименования,
записи на диск или атомарной публикации. #183 применит поверх этого API порядок,
имена и атрибуты штатного `.gbin`; #182 проверит допустимые пути, коллизии,
ссылки, descriptor, перекрытия записей, ZIP-политику и согласованность частей.
Хеши и формат байткода здесь не проверяются. Успешное открытие/чтение ZIP не
означает допустимость модуля Goat.

Ошибки возвращаются кодом, без аварийного завершения и глобального last-error:
INVALID, MEMORY, FORMAT, ZIP64, MULTIDISK, UNSUPPORTED, CAPACITY, SIZE, CRC,
DEFLATE, HEADER_MISMATCH; человекочитаемое имя — `zip_status_string`.
Нулевые указатели/неверные индексы диагностируются; close/free принимают NULL.

Проверка: `python scripts/check_zip.py <zip_testing>`. Независимые образцы
строятся Python zipfile; созданные C writer архивы читаются zipfile. Проверяются
stored/Deflate, data descriptors, дубли/NUL/длинные имена, бинарные комментарии,
пустые части, границы, усечения, CRC, Deflate, размеры и ограничение памяти.
Запись известных частей сравнивается побайтно с замороженным empty.gbin.
Тот же тест входит в Linux x86-64/ARM64 и Windows CI.

## English

`lib/archive/zip_archive.h` is the VM-independent `goat_common` API, using pinned
zlib 1.3.2 and classic MiniZip. The [vendor README](../third_party/zlib/README.goat.md)
records alternatives, licenses, exact commit/checksums, files and local changes.
CMake builds offline, without system zlib or external archive commands.

`zip_archive_open` borrows immutable bytes until close. Enumeration preserves
central-directory order and duplicates. Layout exposes original bytes, EOCD,
comment, actual/declared central offsets, prefix and trailing sizes. Entry and
local accessors expose separate original headers, length-delimited names,
extras/comments, methods, flags, CRC, sizes, timestamps, versions and attributes.
Long names and embedded NUL are preserved. All views borrow input. Broken local
headers return FORMAT while central records remain inspectable.

Opening bounds-checks the directory and cross-checks its index with MiniZip.
ZIP64 EOCD markers, central sentinels and central extra tag 1 fail explicitly;
multidisk has a separate error. Local extras, including ZIP64, remain visible.
Encryption, unsupported methods, link attributes, unusual paths and header
mismatches are never silently normalized into accepted ordinary files.

`zip_archive_read` accepts caller-owned storage and capacity. Declared size is
checked before writes. Bounded streaming inflate verifies stream end, complete
compressed-input consumption, actual size and CRC. Names, methods, flags,
versions and timestamps must agree between headers; CRC/sizes must also agree
without a data descriptor. Errors clear written but may leave partial untrusted
output. Output must not overlap the immutable input archive. No files are
extracted. Reads are repeatable, may use any index, and do not mutate the handle.

`zip_archive_write` takes an ordered entry array, binary archive comment and
maximum output allocation/size. It returns owned memory (release using
`zip_archive_free`), or NULL/0 on error with temporary resources released.
Inputs are borrowed for the call. Names must be nonempty and NUL-free; comments
are arbitrary bytes, extras complete ZIP TLV records without ZIP64. Duplicates
and order are preserved. Each entry explicitly supplies method 0/8, flags 0 or
0x0800, DOS date/time, made-by, attributes, local/central extras and comment.

Compression is pinned: zlib 1.3.2, level 9, raw windowBits=-15, memLevel=8,
Z_DEFAULT_STRATEGY, one final Z_FINISH, no intermediate flush. No stored fallback,
even for empty/tiny/incompressible entries. Version-needed=20, single disk, no
data descriptors or automatic ZIP64. After MiniZip emits the archive, fixed-size
flag, text-attribute, date/time and comment fields are restored to exact caller
values, overriding upstream heuristics. No ZIP/Deflate codec is reimplemented;
no current time or host properties enter the output.

Limits: fewer than 65535 entries; each input and total output below 0xffffffff
bytes and addressable by size_t. No path interpretation, sorting, renaming,
filesystem publication or Goat policy is applied. #183 supplies deterministic
module entry policy and atomic publication; #182 validates paths/collisions,
links, descriptors, overlaps, container policy and cross-part consistency.
Successful ZIP access alone does not establish a valid Goat module.

Errors are returned, never fatal or global: INVALID, MEMORY, FORMAT, ZIP64,
MULTIDISK, UNSUPPORTED, CAPACITY, SIZE, CRC, DEFLATE, HEADER_MISMATCH. Use
`zip_status_string` for diagnostics. Invalid arguments/indices are diagnosed;
close/free accept NULL.

`python scripts/check_zip.py <zip_testing>` uses independent Python zipfile
inputs and checks C output with zipfile, exercises bounds/corruption/metadata,
and compares profile-1 output byte-for-byte with frozen empty.gbin. The same
checks run in Linux x86-64/ARM64 and Windows CI.
