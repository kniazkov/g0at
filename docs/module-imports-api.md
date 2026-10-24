# Static import extraction (#187)

`common/module_imports.h` collects direct exact-version dependencies from the
owned UTF-8 snapshots returned by `module_sources_read`. It performs no I/O,
module search, code execution, optimization or transitive dependency expansion.
CLI integration is #191; archive lookup and whole-graph verification are #188
and #189. Hashes are deliberately not invented here: the later resolver must
attach verified main hashes before writing the strict `module.json` records.

## Resolution and diagnostics

Every syntactic literal `import` in every source counts, including includes,
external sources, unreachable branches and uncalled functions. Comments, string
contents and member names (`object.import`) do not count. The shared scanner uses
Goat string decoding; empty/NUL strings and dynamic operands fail. It checks
lexical validity, balanced brackets and literal operands, not the full surrounding
language grammar; normal parsing remains responsible for that compilation phase.

The shared identity validator requires an exact valid name and optional exact
`@version`. A shorthand import uses only that name's `.gmod` default. Explicit
versions affect only their own occurrence. No default may be inferred from another
import, a transitive dependency or files on disk. Direct-source input has no
`.gmod` defaults, so all imports need explicit versions. Full descriptor validation
precedes collection, including for manually constructed source graphs.

Repeated `(name, version)` pairs produce one dependency with every source site.
Different versions coexist. Dependencies are sorted bytewise by name, then version;
this is a canonical set representation, not initialization or priority order.
Sites retain file index, one-based row/column and zero-based Unicode offset, in
file-index/source order. Callers map indices back to graph logical names when
reporting errors; external basenames need not be unique.

Each default pair unused by any import is returned once in `unused_defaults`,
canonically sorted, without sites. Callers report these as warnings, never include
them in dependencies. An explicit import equal to the default uses that pair;
an explicit different version does not. Informational fields such as `authors`
remain untouched and retain their existing ordering contract.

## Lifetime and bounds

`module_imports_collect` leaves input unchanged and returns independently owned
strings/site arrays, valid after freeing the graph. Release with
`free_module_imports`; NULL is accepted. Failure returns no partial result and a
status/location/static message; no error cleanup is necessary. A NULL error pointer
is supported. `max_imports` limits occurrences, including repeats; zero selects
65,536. The scanner reads each existing bounded source snapshot before this
occurrence limit is checked. Allocation failures retain the compiler's fatal
allocation policy. No archive metadata or build paths are serialized by this API.

`scripts/check_module_imports.py build/module_imports_testing` exercises exact
resolution, deduplication, canonical order, unused-default warnings, include
coverage, unreachable imports, malformed operands/identities and occurrence bounds.
The C harness checks argument validation, invalid in-memory descriptors, independent
result ownership and leak-free cleanup. CI runs these checks on Linux and Windows.

# Статическое извлечение импортов (#187)

`common/module_imports.h` собирает непосредственные зависимости из сохранённых
UTF-8-снимков `module_sources_read`, без чтения файлов, поиска библиотек,
исполнения кода или раскрытия транзитивных зависимостей. Учитываются все литеральные
`import`, включая включённые и внешние исходники, невыполненные ветки и невызванные
функции. Комментарии, содержимое строк и имя члена `object.import` не учитываются.
Динамический аргумент, пустая строка, NUL и неверное имя/версия дают ошибку.
Лексический проход не заменяет последующий полный синтаксический анализ программы.

Импорт без `@version` получает только версию по умолчанию данного имени из `.gmod`.
Явная версия относится только к своему вхождению и не меняет остальные импорты.
Без значения по умолчанию сокращённый импорт запрещён, даже если рядом есть
единственный явный импорт того же имени. Для прямого входного `.goat` все импорты
должны иметь явную версию. Полная схема дескриптора проверяется до сбора импортов.

Одинаковые пары «имя, версия» объединяются с сохранением всех исходных позиций;
разные версии одного имени допустимы. Результат отсортирован по имени и версии
как множество, а не как порядок инициализации. Позиция содержит индекс файла,
строку/столбец с единицы и смещение в Unicode-символах с нуля. Неиспользованные
пары из `.gmod` возвращаются отдельно в `unused_defaults` для предупреждений и
не входят в зависимости. Явный импорт версии по умолчанию считается её
использованием; импорт другой версии — нет. Порядок `authors` не меняется.

Результат владеет строками и позициями, не меняет вход и переживает освобождение
исходного графа; освобождается через `free_module_imports`. При ошибке частичного
результата нет. Лимит вхождений (включая повторы) по умолчанию равен 65 536;
лексический разбор ограниченного снимка файла предшествует проверке этого лимита.
Хеши не подставляются фиктивными значениями: проверенный основной хеш добавит
последующий механизм поиска (#188) и проверки графа (#189), до записи готовых
метаданных. Подключение к CLI относится к #191. Позитивные и негативные фикстуры
выполняются в общей инфраструктуре CI Linux/Windows.
