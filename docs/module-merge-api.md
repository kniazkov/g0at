# Module merger (#215) / Объединение модулей

## English

`goat-merge` combines native variants of one immutable main build. It is a separate
command, not a compiler or a runtime loader:

```sh
goat-merge geometry.gbin windows.gbin linux.gbin
goat-merge geometry.gbin geometry.gbin extra.gbin
goat-merge result source.gbin
goat-merge -- --result --source
```

The first positional path is the destination, followed by one or more sources.
`--help` and `--version` report information without merging; `--` ends options.
There is no `--output` or `--force`. Every path is literal: no extension is added,
no alternate `.gbin` is searched, and `-` names a file, not a standard stream.
Windows paths use the common lossless Unicode-to-UTF-8 argument adapter.

Repeated **exact source argument strings** are removed before opening archives or
counting author lists, retaining the first occurrence. Different spellings, links
or archives with equal main hashes are not collapsed. The destination is not an
implicit source: its previous bytes and metadata are irrelevant unless it also
appears explicitly among the sources. One source is valid and is fully checked
and canonically repackaged, not copied byte for byte.

Sources are checked in argument order using the shared nonexecuting archive
validator. On a damaged source, its diagnostics are emitted and later sources are
not opened. All sources must pass archive, metadata, bytecode-consistency and
native checks; all main hashes must match. Dependencies remain pinned as recorded:
merge neither resolves their versions again nor needs dependency archives.
No bytecode, native library constructors or exported functions are executed.

Native variants with identical requirements and native hash are deduplicated;
different hashes under identical requirements are errors. Ambiguous compatible
variants are rejected rather than ranked. The original DLL/SO bytes and hashes
are preserved. `cCompiler` is carried with its variant: equal name/version pairs
collapse, missing information can be filled from another copy, but different
provided pairs conflict even when a third copy has no information.

Information is handled by the [shared all-input policy](module-information-api.md).
Empty optional values are normalized; description, license and homepage must
agree including presence. Producers form a deterministic union, without adding
the merger as a producer. Zero nonempty author lists need no prompt; one retains
its order. Two or more require a human-selected order even when identical.
All unique authors must appear exactly once: enter a complete permutation of the
shown one-based numbers on one line. Invalid answers retry; EOF or unavailable
terminal input fails without publication. Redirecting stdin cannot supply an
automatic order. Path checks and input/conflict validation precede the prompt.

Sources must resolve to regular files; source symlinks are allowed. A destination
symlink, including a dangling one, or any special object is rejected without
replacement. Its parent must already exist; links in parent paths are allowed.
No directory is created. These checks happen before author interaction and
creating a temporary output; the destination is checked again before publication.

The [shared writer](module-writer-api.md) normalizes metadata, assigns deterministic
native entry names, uses the standard all-Deflate archive profile and fully checks
the prepared temporary archive. It creates the temporary file beside the output
and atomically replaces only that directory entry. Existing permissions, including
applicable ACLs, are read from the destination, transferred and checked **before**
replacement; failure preserves the old file. New output uses ordinary OS creation
permissions, not source permissions. Other hard links retain the old bytes.
There is no delete/copy fallback or separate overwrite confirmation. Visibility is
atomic, but v1 makes no additional power-loss durability guarantee.

Exit codes are **0** for successful publication or valid help/version, **1** for
read, validation, merge or publication failure, and **2** for invalid arguments
(detected before archive processing). Success prints one short human-readable
stdout line containing the destination, module name/version and total native
variant count, including zero. Binary archives never go to stdout. Failed
publication never prints a success message; diagnostics explain the failure.

## Русский

`goat-merge` — отдельная команда объединения нативных вариантов одной неизменной
основной сборки, а не компилятор или загрузчик:

```sh
goat-merge geometry.gbin windows.gbin linux.gbin
goat-merge geometry.gbin geometry.gbin extra.gbin
goat-merge result source.gbin
goat-merge -- --result --source
```

Первый позиционный путь — назначение, следующие — один или несколько источников.
`--help` и `--version` выводят сведения без слияния; `--` завершает разбор опций.
`--output` и `--force` отсутствуют. Пути буквальные: расширение не добавляется,
другой файл с `.gbin` не ищется, `-` — имя файла, а не стандартный поток.
В Windows аргументы преобразуются общим адаптером из Unicode в UTF-8 без потерь.

До чтения и подсчёта списков авторов удаляются повторы **точных строк аргументов
source**, с сохранением первого вхождения. Разные написания путей, ссылки и архивы
с одинаковыми основными хешами не объединяются. Destination не является неявным
источником: прежние байты и метаданные участвуют только при явном перечислении
его среди source. Один источник допустим: он полностью проверяется и канонически
перепаковывается, а не копируется побайтово.

Источники проверяются в порядке аргументов общим неисполняющим валидатором.
При повреждении выводятся диагностики текущего источника; последующие не открываются.
Каждый вход должен пройти проверку архива, метаданных, согласованности байткода
и native; основные хеши должны совпасть. Зависимости остаются закреплёнными:
утилита не выбирает версии заново и не требует архивов зависимостей.
Байткод, конструкторы DLL/SO и экспортируемые функции не исполняются.

Варианты с одинаковыми требованиями и нативным хешем дедуплицируются; разные хеши
при одинаковых требованиях — ошибка. Неоднозначные совместимые варианты отвергаются,
а не ранжируются. Байты DLL/SO и их хеши сохраняются. `cCompiler` переносится вместе
с вариантом: одинаковые пары name/version объединяются, отсутствующие сведения
дополняются из другой копии; разные предоставленные пары конфликтуют, даже если
в третьей копии сведения отсутствуют.

Информацию обрабатывает [общая политика всех входов](module-information-api.md).
Пустые необязательные значения нормализуются; description, license и homepage
должны совпасть, включая наличие. Producers образуют детерминированное объединение
без добавления упаковщика. Ноль непустых списков authors не требует вопроса;
единственный сохраняет порядок. Два и более требуют выбора человека, даже если
совпадают. Все уникальные авторы должны войти ровно один раз: введите одной строкой
полную перестановку показанных номеров от единицы. Ошибочный ответ вызывает повтор;
EOF или недоступность терминального ввода прекращают слияние без публикации.
Перенаправленный stdin не задаёт автоматический порядок. Пути, входы и конфликты
проверяются до вопроса.

Источники должны разрешаться в обычные файлы; ссылки-источники допустимы.
Ссылка в самом destination, включая оборванную, или специальный объект отвергаются
без замены. Родитель должен существовать; ссылки в родительском пути допустимы.
Каталоги не создаются. Проверки предшествуют выбору авторов и временному результату;
перед публикацией назначение проверяется снова.

[Общий писатель](module-writer-api.md) нормализует метаданные, назначает
детерминированные имена native-частей, использует стандартный профиль с Deflate для
всех файлов и полностью проверяет подготовленный временный архив. Временный файл
создаётся рядом с результатом; атомарно заменяется только запись destination.
Прежние права, включая применимые ACL, читаются у назначения, переносятся и
проверяются **до** замены; отказ сохраняет старый файл. Новый результат получает
обычные права создания ОС, не права источника. Другие жёсткие ссылки сохраняют
старые байты. Нет обхода через удаление/копирование и дополнительного подтверждения
перезаписи. Видимость атомарна; отдельная гарантия сохранности при отключении питания
в v1 не вводится.

Коды завершения: **0** — публикация или корректная справка/версия; **1** — ошибка
чтения, проверки, объединения или публикации; **2** — неверные аргументы, выявленные
до обработки архивов. После публикации stdout содержит одну короткую человеческую
строку с путём, именем/версией модуля и общим числом native-вариантов, включая ноль.
Бинарный архив в stdout не пишется. При отказе сообщения об успехе нет; причина
объясняется диагностикой.
