# Source include execution / Исполнение include (#194)

## English

`common/module_compilation.h` compiles the immutable source snapshots from
`module_sources_read`, not paths reopened during code generation. Each source is
parsed and analyzed in an isolated lexical environment. Literal include occurrences
are bound to the source graph's local file indices before expression reduction;
the emitted `MODULE_INCLUDE` instruction contains a `uint32` index, never a path.
The compiler uses one code/data builder and the shared linker, preserving file
entries, declaration provenance, function ownership and source locations. All
sources are checked, including includes in unexecuted branches and functions.

The runtime resolves the index in the current module instance. It reuses
`initialize_module_file`: the first execution uses the persistent file scope and
an isolated frame, repeated and cyclic requests return that same scope, and a
failed initializer rethrows its saved value and original trace without repeating
side effects. The caller's operand stack, instruction pointer and auxiliary
arguments survive a nested include. Caller locals and receiver are not inherited;
normal access rules and builtins remain available. The shared immutable image is
unchanged by execution.

Paths are resolved only by the existing source-graph builder. A relative include
starts at the real containing source directory after symlink resolution. Symlink
aliases share a file index; distinct hardlink paths and equal-content files do
not. Included code is stored in the archive, so moving or deleting every original
source does not affect execution of a validated `.gbin`.

The `source_include_testing` component exercises the production compiler and VM:

- `run <source.goat|descriptor.gmod>` compiles and executes the entry source;
- `compile <source.goat|descriptor.gmod> <output.gbin>` writes a validated module;
- `binary <output.gbin>` validates and executes the saved module without sources.

These are test-component commands, not new public CLI flags. Full CLI integration,
module imports and native module generation keep their separate checklist tasks
(#191, #195, #211). This step uses conservative, non-optimizing source analysis;
it does not introduce a second optimizer or a second initialization state machine.

Run `python3 scripts/check_source_include.py build/source_include_testing`.
The suite checks nested/repeated/lazy inclusion, argument-stack preservation,
function and cycle behavior, failure caching, file access boundaries, literal-only
operands, descriptor input, canonical symlink bases, hardlink independence and
source-free archive execution. Existing wire validators cover invalid indices and
file-entry relationships. Symlink setup is omitted only when Windows explicitly
reports the missing symlink privilege; hardlink and ordinary-file tests remain.

## Русский

`common/module_compilation.h` компилирует неизменяемые снимки из
`module_sources_read`, не открывая пути повторно при генерации. Каждый исходник
разбирается и анализируется в отдельной лексической среде. Литеральные include
связываются с локальными индексами файлов графа до свёртки выражений.
`MODULE_INCLUDE` хранит индекс `uint32`, а не путь. Общие построители кода/данных
и линкер сохраняют точки входа, происхождение объявлений, принадлежность функций
и позиции. Проверяются также исходники из невыполняемых ветвей и функций.

VM разрешает индекс в текущем экземпляре модуля и использует существующий
`initialize_module_file`. Первый вход исполняет файл в постоянной области и
изолированном кадре, повторный и циклический возвращают ту же область. Неудача
повторно выбрасывает сохранённое значение и исходную трассировку, не повторяя
эффекты. Вложенный include сохраняет стек операндов, адрес инструкции и
вспомогательные аргументы вызывающего кода. Его локальные переменные и получатель
не наследуются; действуют обычные права доступа и встроенные функции.
Исполнение не изменяет общий образ байткода.

Пути обрабатывает только готовый построитель графа. База относительного include —
каталог настоящего содержащего файла после разрешения символических ссылок.
Символические псевдонимы разделяют индекс; разные пути жёстких ссылок и отдельные
файлы с одинаковым содержимым не объединяются. Код всех исходников находится в
архиве: удаление или перенос исходников не влияет на запуск проверенного `.gbin`.

Компонент `source_include_testing` предоставляет команды `run`, `compile` и
`binary`, перечисленные выше. Это тестовый интерфейс реального компилятора и VM,
не новые флаги пользовательской CLI. Полная CLI-интеграция, import и нативная
генерация остаются отдельными задачами #191, #195, #211. Здесь используется
консервативный анализ без оптимизаций, а не отдельный оптимизатор.

Набор `scripts/check_source_include.py` проверяет вложенность, повторение,
ленивость, циклы, ошибки, изоляцию областей, литеральные пути, `.gmod`, символические
и жёсткие ссылки и запуск архива без исходников. Проверки диапазонов индексов и
точек входа остаются в общих валидаторах. На Windows допускается только явное
отсутствие привилегии создания символических ссылок; остальные проверки выполняются.
