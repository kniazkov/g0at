# Проверка .gbin / .gbin container validation

## Русский

`src/module/module_container.h` предоставляет переносимый валидатор контейнера
v1 в `goat_module`. Он использует общую ZIP-обёртку #247, JSON-читатель #178,
нормализацию #180 и основной SHA-256 #181. Второй ZIP/Deflate реализации нет.

`goat_module_container_read(data, size, max_expanded, &container, &error)`
принимает неизменяемые байты ZIP на время вызова. Успешный результат владеет
нормализованными метаданными, точным байткодом и байтами всех native-вариантов.
Исходный ZIP после возврата можно освободить. Accessors возвращают заимствованные
неизменяемые views до `goat_module_container_free`; байткод не переписывается.
Native views идут в нормализованном порядке метаданных, не в порядке ZIP.

При любой ошибке результат NULL: частично проверенный модуль недоступен.
Диагностика содержит общий код, исходный индекс записи центрального каталога
(SIZE_MAX для общей ошибки) и вложенные ZIP/JSON-коды. error необязателен.
MEMORY и LIMIT — ошибки загрузки, а не отсутствие модуля: вызывающий загрузчик
обязан завершить загрузку, без поиска другой копии. free принимает NULL.

До распаковки проверяются:

- Обычный однодисковый ZIP32; ZIP64 запрещён и в локальных extras/размерах.
  Методы только stored/Deflate; никаких шифрования, patched data и неизвестных
  флагов. Разрешены UTF-8, data descriptor и Deflate level flags. Версия извлечения
  10–20; Deflate/data descriptor требуют 20.
- Нет префикса/хвоста. Локальные записи полностью занимают область до центрального
  каталога: нет дыр, пересечений, повторно используемых смещений или скрытых записей.
  Порядок local records может отличаться от порядка central records.
- Local/central согласованы по имени, версии, методу, флагам, времени и дате.
  Без descriptor CRC/размеры совпадают; с descriptor локальные поля могут быть
  нулевыми или совпадающими. Descriptor занимает ровно 12 байт без подписи либо
  16 с подписью 0x08074b50; CRC и оба размера совпадают с central.
- Все имена уникальны также после ASCII case folding. Разрешены только
  `module.json`, `bytecode.bin`, необязательный пустой `native/` и плоские native
  пути по схеме. NUL, backslash, абсолютные пути, `..`, ADS, вложенные каталоги,
  недопустимые символы и зарезервированные DOS stems (con/prn/aux/nul/com0..9/
  lpt0..9) запрещены. Ничего не исправляется и не нормализуется как путь.
- Только обычные файлы и каталог native/. Проверяются Unix type bits даже при
  чужом made-by; запрещены ссылки, устройства, сокеты, FIFO, DOS volume/device и
  Windows reparse attributes. Unix link extras отвергаются. Extras должны быть
  целыми TLV records; ZIP64, encryption и alternate Unicode path extras запрещены.
  Пассивные/неизвестные extras и комментарии остаются неприменяемыми данными.
- Сумма заявленных распакованных размеров представима в size_t и не превышает
  max_expanded. Это бюджет содержимого, не оценка всей памяти JSON DOM/индекса.
  SIZE_MAX означает только предел адресного пространства; произвольного лимита
  формата или compression ratio нет. Слишком большой объём не обрезается.

Затем строго читается module.json и проверяется точное соответствие native-файлов
объявленным library paths: каждый присутствует ровно один раз, посторонних нет.
CRC, точный распакованный размер и завершение Deflate проверяются у каждой записи,
включая пустой каталог. Вычисляются mainHash по канонической проекции и точному
байткоду, SHA-256 полных native-байтов по каждому объявленному hash. Проверяются
все платформы и архитектуры, без отбора под текущую ОС. Корректные stored,
Deflate, смешанные архивы, другие даты/права и безопасный native/ допустимы.

Это ещё не полная проверка исполняемого модуля: #184 проверяет структуру и таблицы
байткода, #208/#209/#233 — native-описание/ABI/согласованность, последующие этапы —
граф зависимостей и семантические связи. Хеш не подменяет эти проверки. Валидатор
не запускает VM, не вызывает загрузчик ОС и не извлекает файлы на диск. #185
использует этот слой в общем API; #183 отвечает за запись и атомарную публикацию.

Проверка: `python scripts/check_module_container.py <module_container_testing>`.
Архивы создаются независимым Python zipfile, затем целенаправленно повреждаются.
Положительные случаи включают эталон empty.gbin, оба метода, оба descriptor,
перестановки, extras/comments и чужие native-варианты. Отрицательные — пути,
атрибуты, дубли, структуры, размеры, CRC, хеши и все усечения образца. C fixture
затирает/освобождает исходный ZIP до доступа к результату, проверяя владение.
Тесты включены в Linux/Windows CI; обе команды сборки создают fixture явно.

## English

`src/module/module_container.h` adds the portable v1 container validator to
`goat_module`, reusing ZIP #247, strict JSON #178, normalization #180 and hash
#181. It does not introduce another ZIP/Deflate implementation.

`goat_module_container_read(data, size, max_expanded, &container, &error)` borrows
immutable ZIP bytes only during the call. Success owns normalized metadata,
exact bytecode and all native payloads; the caller may release the ZIP. Accessors
borrow immutable views until free. Bytecode is never rewritten. Native views
follow normalized metadata order. Failure returns NULL, never a partial module.
Optional diagnostics include status, original central-directory index (SIZE_MAX
for global errors), and nested ZIP/JSON codes. MEMORY/LIMIT are load failures,
not module-not-found; callers must propagate them without trying another copy.

Before decompression the validator checks ZIP32/single disk, stored/Deflate,
no encryption/unknown flags, supported extraction versions (10–20, 20 required
for Deflate/descriptors), local/central agreement and unique case-folded paths.
No prefix, trailer, gaps, overlapping or aliased records are permitted. Physical
local order may differ from central order. Descriptors must occupy exactly 12
bytes without signature or 16 with signature and match CRC/compressed/expanded
sizes. With descriptors, local CRC/size fields may be zero or exact; otherwise
they must match central fields.

Only module.json, bytecode.bin, optional empty native/ and declared flat native
paths are allowed. Embedded NUL, backslash, traversal, absolute paths, ADS,
nesting, invalid characters and reserved DOS device stems are rejected unchanged.
Unix type bits, DOS device/volume/directory and Windows reparse attributes are
checked independently of made-by; only regular files/directories are accepted.
Unix link extras are rejected. Extras must be complete TLV records; ZIP64,
encryption and alternate Unicode path records are forbidden. Passive/unknown
extras and comments are never interpreted as extraction instructions.

Declared expanded sizes are summed with overflow checks before allocations and
must fit size_t and max_expanded. The budget covers payload sizes, not all DOM
and index overhead. SIZE_MAX applies only the process address-space limit; no
arbitrary format size or compression-ratio threshold is introduced.

Strict metadata parsing is followed by exact native-name membership checks;
missing and undeclared files fail. Every entry, including the empty directory,
undergoes bounded expansion, exact-size, Deflate completion and CRC checks.
mainHash covers the canonical projection and exact bytecode; each native hash
covers all original library bytes, including foreign platforms/architectures.
Stored, Deflate, mixed archives, other timestamps/ordinary permissions and safe
native/ entries remain valid.

This is not full executable-module validation. Bytecode tables/structure (#184),
native metadata/ABI/consistency (#208/#209/#233), dependency graphs and semantic
cross-checks remain subsequent stages. A matching hash never substitutes for
those checks. No filesystem extraction, VM execution or OS library loading
occurs. #185 integrates the public module API; #183 writes/publishes archives.

Run `python scripts/check_module_container.py <module_container_testing>` for
independent zipfile fixtures, frozen vectors, methods/descriptors, metadata,
foreign variants and malformed inputs. The C fixture overwrites/frees the input
before inspecting the owned result. Linux and Windows CI run this suite, and
both build scripts explicitly build the fixture.
