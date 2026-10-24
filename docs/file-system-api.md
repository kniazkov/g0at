# Файлы и каталоги / Files and directories

## Русский

`src/lib/file_system.h` — общий C API Linux/Windows. Пути — UTF-8 без нормализации и изменения регистра. Для строк с известной длиной сначала вызывается `validate_fs_path`: встроенный NUL, пустая строка и неверный UTF-8 отклоняются. Функции с C-строкой видят только байты до завершающего NUL. Windows использует строгие UTF-8/UTF-16 преобразования и W API; Linux передаёт байты без изменения. Непредставимое имя при перечислении или уборке означает ошибку, а не пропуск.

`fs_error_t error = {0}` хранит категорию, платформенную причину, операцию и собственную копию пути. После использования нужен `clear_fs_error`. Ошибки stream-операций содержат CRT errno, прочие — errno Linux / Win32 error Windows. Слой не выводит диагностику и не завершает процесс из-за файловой ошибки; выделение памяти сохраняет общую политику ALLOC проекта. Ошибка остаётся до явной очистки или следующей ошибки: успех не очищает предыдущую. NULL вместо error отключает сохранение диагностики.

`inspect_fs_path(path, follow_links, ...)` явно выбирает следование конечной ссылке. Без следования любой Windows reparse point имеет тип FS_LINK. Отсутствие, отказ доступа, существующий объект, неподдерживаемая гарантия и прочие ошибки различаются. `absolute_fs_path` делает путь абсолютным без разрешения ссылок и без требования существования; `join_fs_path` соединяет компоненты, но не является проверкой изоляции пути.

`FOPEN` принимает UTF-8 и режимы rb/wb/wbx; последний создаёт файл исключительно. `create_fs_directory` создаёт обычный каталог. `create_fs_temporary(prefix, directory, &file, &error)` атомарно создаёт случайное имя рядом с prefix, без проверки «свободно ли имя» перед созданием. Файл возвращается открытым; каталог — закрытым. Linux: файлы 0666 с umask/default ACL родителя, временные каталоги 0700, обычные каталоги 0777 с umask/default ACL. Windows: наследование security descriptor от родителя. Владельцу необходимо ограничить доступ к рабочему каталогу, если содержимое конфиденциально. Возвращённые строки освобождаются FREE. `list_fs_directory` возвращает полный список имён без `.`/`..`, порядок не задан; `clear_fs_entries` освобождает список. При ошибке частичный список не возвращается.

`remove_fs_tree` предназначен только для принадлежащего вызывающему дерева. На Linux обход привязан к дескрипторам каталогов (`openat`, `fstatat`, `unlinkat`, O_NOFOLLOW); на Windows — к handles (`NtCreateFile` с RootDirectory и FILE_OPEN_REPARSE_POINT, перечисление и удаление по handle). Ссылки/reparse points удаляются сами, их цели не обходятся. Проверяется смена идентичности при открытии; Windows дополнительно запрещает переименование открытого объекта. Linux не пересекает границу другого устройства. Глубина ограничена 256. Подмена или изменение дерева может привести к частичной уборке и ошибке — транзакционность удаления не обещается. Открытый каталог Linux остаётся тем же принадлежащим вызывающему объектом даже после переименования. Это не защита от привилегированного изменения mount namespace или внесения чужих обычных файлов в принадлежащее вызывающему дерево. Жизненным циклом, повторными попытками и сроками занимается #245.

Атомарная публикация состоит из отдельных шагов:

```c
fs_error_t error = {0};
FILE *file = NULL;
char *temporary = create_fs_temporary(destination, false, &file, &error);
if (temporary) {
    bool ok = write_fs_file(file, bytes, size, temporary, &error);
    if (!close_fs_file(file, temporary, &error))
        ok = false;
    if (ok)
        ok = prepare_fs_replace(temporary, destination, &error);
    /* Здесь потребитель проверяет полностью записанный temporary. */
    if (ok)
        ok = publish_fs_replace(temporary, destination, &error);
    if (!ok)
        remove_fs_entry(temporary, NULL);
    FREE(temporary);
}
clear_fs_error(&error);
```

Подготовленный файл должен быть обычным, без других жёстких ссылок; вызывающий владеет им исключительно и сериализует изменение destination между подготовкой и публикацией. Для существующего обычного destination копируются Linux uid/gid/mode/POSIX access ACL, Windows owner/group/DACL. Windows копия DACL защищена от повторного наследования, чтобы сохранить эффективные права. Прочие xattrs, timestamps, Windows SACL аудита, integrity labels и POSIX capabilities не копируются: это не API архивного копирования. Отсутствие поддержки обязательного ACL или ошибка переноса прав останавливает подготовку. Для нового destination и конечной ссылки сохраняются права создания временного файла; публикация заменяет саму ссылку, а не её цель.

`rename_fs_path` переименовывает объект, а `publish_fs_replace` дополнительно требует обычный подготовленный файл с одной жёсткой ссылкой. Оба выполняют только rename/MoveFileExW без COPY_ALLOWED: другая файловая система, блокировка Windows, неверный тип и прочие отказы не приводят к delete/copy fallback. Все проверки и ошибки закрытия должны быть обработаны до публикации. Другие жёсткие ссылки на старый destination сохраняют прежний inode и содержимое. Гарантия — видимость старого либо полностью подготовленного результата, не сохранность после отключения питания. Враждебные конкурентные изменения destination вне указанного контракта не поддерживаются.

`fs_arguments` на Windows получает исходную Unicode-командную строку, а `fs_environment` — Unicode-значение окружения. После адаптации argv освобождается `free_fs_arguments`. CLI Goat, пути временных каталогов и CC используют эти адаптеры. `binary_file`, текстовый io, path, native-компилятор, извлечение runtime и Windows loader используют общий слой. Будущие goat-inspect/goat-merge должны использовать тот же входной адаптер.

Unit-проверки запускаются в дочернем процессе с таймаутом: конкурентное создание, Unicode вне BMP, отсутствие/отказы, запись/закрытие, ACL, ссылки, сохранность результата при ошибках и жёсткие ссылки. CLI-проверка Unicode-путей включена в существующий кроссплатформенный стенд.

Начиная с #186, `canonical_fs_path` разрешает существующий путь и символические
ссылки; идентичность — получившееся имя, а не inode, поэтому жёсткие ссылки не
объединяются. `read_fs_regular_file` проверяет тип фактически открытого объекта
до чтения, отклоняет FIFO без ожидания писателя и читает не более заданного лимита.
Буфер принадлежит вызывающему (`FREE`), имеет дополнительный NUL за явной длиной;
при ошибке выходы очищены. На Windows оба API отвергают пути относительно диска
и корня текущего диска; канонический результат использует `/`.

## English

`src/lib/file_system.h` provides UTF-8 filesystem operations for Linux and Windows. Bounded input must pass `validate_fs_path`, which rejects embedded NUL, empty paths and malformed UTF-8. C-string APIs necessarily stop at NUL. Windows uses strict UTF-16 conversion and W APIs; Linux preserves bytes, case and normalization. Unrepresentable directory names fail enumeration/cleanup explicitly.

Initialize errors with `{0}` and release them with `clear_fs_error`. Errors retain category, operation, owned path and native reason (CRT errno for stream operations, otherwise errno/Win32). Successful calls do not erase earlier errors. No filesystem failure prints or exits; allocation retains the project's ALLOC policy. Path joining is not a confinement check. Absolute paths are lexical and need not exist.

Temporary names use OS randomness and exclusive creation, including concurrent calls. Files use normal parent creation policy (Linux 0666 with umask/default ACL; Windows inherited security). Linux temporary directories use 0700; ordinary directories use 0777 with creation policy. Callers must protect sensitive workspaces. Lists exclude dot entries and are all-or-error, with unspecified order; free them with `clear_fs_entries`.

Owned-tree removal uses directory-relative descriptors on Linux and native relative handles on Windows. Final links and all reparse points are removed without visiting their targets. Identity changes produce errors; Windows handles prohibit rename while open. Linux rejects cross-device descent. Depth is limited to 256. Concurrent mutation can cause partial cleanup and an error. An opened Linux directory remains the caller-owned object after rename. This does not defend against privileged mount changes or another actor placing unrelated ordinary files inside the owned tree. Retry/lifetime policy belongs to #245.

Publication separates checked write/close, permission preparation, consumer validation and atomic rename. See the C example above. The caller exclusively owns the unpublished regular file (one hard link) and serializes destination changes across these phases. Existing regular destinations contribute Linux uid/gid/mode/POSIX access ACL or Windows owner/group/DACL; copied Windows DACLs are protected against reinheritance. Audit SACLs, integrity labels, capabilities, other xattrs and timestamps are outside this access-control contract. Required ACL support or permission-copy failure aborts preparation. New destinations and final links retain temporary creation permissions; rename replaces the link itself.

`rename_fs_path` renames an entry; `publish_fs_replace` additionally requires a regular file with one hard link. Neither falls back to delete/copy, including cross-filesystem failure. Other hard links retain the old contents. The guarantee is atomic visibility, not crash/power-loss durability. Callers must check writes, close and validation before publishing. Concurrent hostile destination changes are outside the serialization contract.

`fs_arguments` and `fs_environment` preserve original Windows Unicode input. `free_fs_arguments` releases the copied argv. The interpreter, binary/text I/O, path helpers, native compilation/runtime extraction and Windows loading now use this layer. Future inspect/merge CLIs should reuse the adapter. Unit tests cover Unicode, exclusive concurrent creation, errors, ACLs, links, checked I/O and old-result preservation; the existing CLI Unicode suite also exercises non-BMP paths.

For #186, `canonical_fs_path` resolves an existing path and symbolic links. The
resulting path, not inode/file ID, is the identity: different hard-link paths stay
distinct. `read_fs_regular_file` checks the actual opened object's type before
reading, rejects FIFOs without waiting for a writer, and bounds the read by its
explicit limit. The caller owns the buffer (`FREE`); a convenience NUL follows the
explicit length, and failures clear both outputs. Both APIs reject incomplete
Windows drive-relative/current-drive-root-relative paths. Canonical Windows names
use `/` separators. Inputs are never expanded or trimmed.
