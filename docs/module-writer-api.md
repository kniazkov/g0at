# Deterministic module writer / Детерминированная запись модуля

The shared API is `src/vm/module_writer.h`, linked through `goat_validation`.
It is packaging infrastructure for the later compiler and goat-merge integrations,
not a new command-line tool. Inputs are borrowed metadata, exact portable bytecode,
and named native byte buffers. Their supplied main/library hashes must already be
correct: the writer does not repair invalid input or recompute its identity.

`module_write_memory` validates and normalizes metadata, validates the original
parts through the shared nonexecuting validator, renames native entries in the
specified normalized variant order, and emits canonical full JSON without a
newline. Matching is by the original library name, never array position. It then
validates the complete result before exposing owned bytes. Missing, duplicate or
undeclared payloads fail. Bytecode, native library contents, main hash and author
order remain unchanged. All payloads must be present before calling, including
when the future destination was also an input.

ZIP writer profile 1 is fixed in `module-format-v1/README.md`: vendored zlib 1.3.2,
raw Deflate level 9, windowBits -15, memLevel 8, default strategy, one final flush.
Every file uses Deflate, even empty or incompressible native bytes. Entry order is
module.json, bytecode.bin, then unsigned-ASCII native filenames. Fixed attributes,
timestamps, no directory entries/comments/extras/descriptors and no ZIP64 match
the existing reference archive byte for byte. Equal inputs produce equal bytes
on every supported platform. This does not deduplicate variants or resolve
informational conflicts; those belong to goat-merge. Different valid input
metadata representations can have different native tie-break names before
renaming; arbitrary repeated repackaging is not a separate fixed-point promise.

`module_write_file` checks the destination, builds the valid result in memory,
creates a unique sibling temporary file, checks write and close, and validates
that actual temporary file. Before publication it verifies the destination has
not detectably changed, transfers and verifies permissions using the shared
filesystem layer, checks the destination again, then atomically renames. It never
truncates/deletes the old file or falls back to copying. Final symlinks, dangling
links and special files are rejected; parent directories must exist. Existing
output contents need not be a valid module and are not implicitly input. New
files follow OS creation rules. Existing hard links keep the old inode/contents.
Caller serialization is required: the final checks are not an adversarial-race
security boundary. Power-loss durability is not promised.

Initialize errors with `{0}` and clear them with `clear_module_writer_error`.
The original failure and failed temporary-file cleanup are reported separately.
NULL options use address-space/ZIP32 limits; explicit limits cap archive and
expanded result sizes. Failed memory writes clear both output pointer and size.
Free successful memory output with `free_module_writer_bytes`.

Tests: `scripts/check_module_writer.py build/module_writer_testing` compares the
checked-in golden archive, validates canonical metadata and native associations,
checks all ZIP header fields, limits and malformed inputs, repeated writes,
publication paths/permissions and injected write/close/validation/permission/
rename failures. The sibling `module_writer_fault_testing` uses the same production
source with test-local call substitution; no fault hook is exposed in the API.
The existing GitHub matrix runs these checks on Linux x86_64/AArch64 GCC/Clang and
Windows MINGW32/MINGW64/UCRT64. No host-Python zlib version is required by the tests.

## Русский контракт

Общий API `src/vm/module_writer.h` предоставляет запись в память и атомарную
публикацию в файл. Это инфраструктура для следующих задач компилятора и
`goat-merge`, а не их преждевременная реализация. Входные метаданные, байткод и
полные байты библиотек заимствуются только на время вызова. Хеши должны быть
правильными заранее: повреждённый вход не исправляется нормализацией или
переименованием. Исходные части и готовый результат проверяются единым API без
исполнения, поиска зависимостей и загрузки библиотек средствами ОС.

Библиотеки сопоставляются по исходному имени, затем получают имена
`native/variant-N.dll` либо `.so` в нормализованном порядке вариантов. Полный JSON
каноничен, без перевода строки. Порядок ZIP-записей, атрибуты и сжатие строго
соответствуют профилю 1: встроенный zlib 1.3.2, Deflate для каждой записи без
перехода на stored. Одинаковые входы дают одинаковые байты на поддерживаемых ОС.
Основной хеш, байткод, байты библиотек и порядок авторов не меняются. Разрешение
конфликтов и объединение одинаковых вариантов остаются обязанностью goat-merge;
новое обещание неподвижной точки произвольной повторной переупаковки не вводится.

Файловая запись создаёт временный файл рядом с назначением, проверяет запись,
закрытие и фактическое содержимое, переносит права доступа и только затем
атомарно заменяет назначение. Символьные ссылки и специальные объекты в качестве
назначения запрещены. Родительский каталог должен существовать. Старый файл
сохраняется при любой ошибке до успешного переименования; удаления с последующим
копированием нет. Ошибка удаления временного файла сообщается отдельно от
первоначальной ошибки. Другие жёсткие ссылки сохраняют прежнее содержимое.
Проверяется обнаруживаемая подмена назначения, но вызывающий код обязан
сериализовать изменения пути; гарантия устойчивости при аварийном выключении
питания не заявляется.

Тесты сверяют эталонные байты, метаданные и связи библиотек, заголовки ZIP,
пределы размеров, ошибки входов и атомарной публикации, включая внедрённые сбои.
Они запускаются на всей существующей матрице CI; версия zlib системного Python
для этих тестов несущественна.
