# Сборка и запуск модулей / Building and running modules

## Русский

CLI соединяет общие API исходников, импортов, проверки графа, writer, снимка
исполнения и native. Ни один из этих этапов не заменяется отдельным парсером
формата или вторым сервисом очистки.

### Команды

```sh
goat build project/app.gmod --native off
goat --compile project/app.gmod --module-dir ./output --native off
goat project/app.gmod --native off
goat run project/app.gmod --native off
goat output/app/1.0.0.gbin --module-dir ./output
goat run output/app/1.0.0.gbin --module-dir ./output
goat --list-native-targets --cc gcc --native-probe-timeout 10
```

`build` и `--compile` с `.gmod` только собирают и публикуют архив, не исполняя
Goat-инициализаторы и не загружая DLL/SO. Прямой запуск `.gmod` и `run .gmod`
собирают снимок в памяти и исполняют его без публикации постоянного архива.
Прямой запуск `.gbin` и `run .gbin` исполняют готовый образ без C-компилятора.
Сохраняются отдельные исходники `.goat`, прежние `--compile`/`--run` и чтение
прежнего отдельного бинарного образа. `--run` остаётся режимом бинарного входа;
команда `run` также принимает исходник или описание. `build` требует `.gmod`.

Модульный компилятор сохраняет консервативный конвейер без агрессивного
переписывания AST при обоих значениях `--optimize`: обязательный анализ интерфейса
исполняется и при none. Параметр не обещает отдельной all-оптимизации модулей.
Экспорт C, исходного текста, анализа и графа для модульного входа пока отклоняется
явной диагностикой; существующие возможности одиночного исходника сохраняются.

Опции можно указывать и после входного имени. `--` завершает разбор опций:
последующие значения передаются программе буквально; если вход ещё не задан,
первое такое значение становится входным именем. Это позволяет передать аргумент,
совпадающий с именем опции. Команда перечисления целей не принимает программу
и не совмещается со сборкой или выбором режима native.

### Пути, проверки и публикация

Сначала полностью проверяется строгая схема `.gmod`, включая неиспользуемые
зависимости и переопределяемые native-поля. Только затем применяются допустимые
значения по умолчанию и CLI-переопределения. `--native off` не исправляет
ошибочный `native.mode`; `--native-target` не скрывает неверный тип target.
В off компилятор не ищется и не запускается.

Реальный путь `.gmod` фиксируется с разрешением символических ссылок. Его каталог
задаёт базу относительного main и локального `modules`. Выход сборки по умолчанию:
`<каталог-настоящего-gmod>/modules/<name>/<version>.gbin`.
Явный `--module-dir` задаёт выходной корень относительно начального cwd, а не
каталога описания. Поиск: настоящий локальный `modules`, явный каталог, затем
`GOAT_MODULE_PATH`; для готового архива локальный `modules` находится рядом
с самим архивом. Корень хранилища из расположения архива не угадывается.
Один зафиксированный набор поиска обслуживает весь граф зависимостей.

Корректные неиспользуемые зависимости дают предупреждения, но их архивы не
ищутся. Использованные зависимости и весь достижимый граф проверяются до
публикации или исполнения. Ошибка первой найденной копии не разрешает искать
другую. Перед заменой выходного файла проверяется окончательный именованный
архив с тем же набором поиска, включая обратные зависимости через установленную
старую копию корня. Writer публикует результат атомарно; неуспешная сборка
не заменяет прежний архив. Отсутствующие выходные каталоги создаются.

Исходник с include/import проходит тот же многофайловый конвейер; отдельному
`.goat` не нужны имя и версия распространяемого модуля, но каждый import обязан
содержать точную версию. `--compile` такого многофайлового исходника сохраняет
соседний ZIP `.gbin`; прежний одиночный конвейер сохраняет отдельный бинарный
образ. Это не заменяет `.gmod` для публикации именованной библиотеки.
Снимки, области, native и диагностические сведения живут
до завершения всех потоков. Ошибки графа и исключения выводятся общими форматтерами,
без повторного чтения исходников или архивов.

### Native и сведения о производителе

Для `.gmod` режим и цель берутся из описания (по умолчанию auto/host), а явно
переданные `--native` и `--native-target` переопределяют их независимо. Готовые
ZIP-модули по умолчанию используют auto; прежний режим прямого `.goat` сохраняется.
Компиляция и запуск разделяют один сервис очистки. Предварительный отказ его
готовности в auto не порождает повторных предупреждений при переходе от сборки
к исполнению; required завершается с ошибкой. Отказ уже готового уборщика не
меняет код программы и не разрешает удалять файлы из-под работающего native.

`--list-native-targets` показывает только цели с успешной пробной сборкой,
их описание, проверенный компилятор и автоматический выбор. Неудачные пробы
показаны отдельно с причинами. Хотя бы одна успешная цель даёт код 0;
отсутствие успешных целей даёт ненулевой код. `--cc` ограничивает исследование
единственным компилятором, окружение наследуется. `--native-probe-timeout`
принимает положительное целое число секунд (по умолчанию 10), ограничивает
пробу, а не компиляцию пользовательского C. Пробы не загружают библиотеку.
Кеш результатов существует только в текущем процессе.

Обычная сборка добавляет одну запись `producers` с именем, версией и стабильным
идентификатором сборки компилятора. Это не время, путь или случайный ID упаковки;
сведения исключены из mainHash и не заменяют проверки формата или ABI.

Проверки CLI находятся в `scripts/check_module_cli.py`; существующие проверки
исходников, бинарного запуска, compiler discovery, native и очистки сохраняются.
Результат тестов подтверждается CI каждого изменения, а не наличием документации.
Ограниченное завершение `exit` реализовано в #230 ([API](module-exit-api.md));
инструменты [inspect](module-inspect-api.md) и [merge](module-merge-api.md) также реализованы.

## English

The CLI composes the shared source, import, graph-validation, writer, execution
snapshot and native APIs. It does not introduce another format parser or cleanup
service.

### Commands

```sh
goat build project/app.gmod --native off
goat --compile project/app.gmod --module-dir ./output --native off
goat project/app.gmod --native off
goat run project/app.gmod --native off
goat output/app/1.0.0.gbin --module-dir ./output
goat run output/app/1.0.0.gbin --module-dir ./output
goat --list-native-targets --cc gcc --native-probe-timeout 10
```

`build` and `--compile` with `.gmod` build and publish only: no Goat initializer
runs and no DLL/SO is loaded. Direct `.gmod` and `run .gmod` compile an in-memory
snapshot and execute it without publishing a persistent archive. Direct `.gbin`
and `run .gbin` execute a ready image without a C compiler. Standalone `.goat`,
legacy `--compile`/`--run` and the existing standalone binary image remain
supported. `--run` is still binary-input mode; the `run` command also accepts
sources and descriptions. `build` requires `.gmod`.

The module compiler keeps its conservative pipeline without aggressive AST rewriting
for either `--optimize` value: mandatory interface analysis runs even with none.
The option does not promise a separate all-optimization pass for modules. C/source,
analysis and graph exports for module input are currently rejected with an explicit
diagnostic; existing standalone-source exports are preserved.

Options may follow the input name. `--` ends option parsing: later values are
literal program arguments, or the first becomes the input name when none has
been supplied. This permits an argument whose spelling is also a CLI option.
Target listing accepts no program and cannot be combined with build/native-mode
selection.

### Paths, validation and publication

Validate the entire strict `.gmod` schema first, including unused dependencies
and native fields which the CLI will override. Only then apply valid defaults
and overrides. `--native off` cannot repair an invalid native.mode, nor can
`--native-target` hide an invalid target type. Off does not discover or invoke
compilers.

Resolve symlinks in the `.gmod` path before fixing relative main and local modules.
Default output is `<real-gmod-directory>/modules/<name>/<version>.gbin`.
Explicit `--module-dir` selects an output root relative to the initial cwd, not
the description directory. Search order is real local modules, the explicit root,
then `GOAT_MODULE_PATH`; a ready archive uses modules beside the actual archive.
Do not infer a repository root from its archive layout. The same frozen search
set serves the whole dependency graph.

Valid unused dependency entries produce warnings without archive lookup. Used
dependencies and the complete reachable graph are checked before publication or
execution. A bad first matching copy never permits trying another. Before output
replacement, validate the final named archive using the same search set, including
back-edges through an already installed old root. Publication is atomic; failure
preserves the old archive. Missing output directories are created.

Sources containing include/import use the same multifile pipeline. A standalone
`.goat` needs no distributable module identity, but every import specifies its exact
version. `--compile` for such a multifile source saves an adjacent ZIP `.gbin`,
while the legacy standalone pipeline keeps its existing binary image. This does
not replace `.gmod` for publishing a named library. Snapshots, scopes, native
resources and diagnostic metadata survive all
program threads. Shared formatters report graph failures and exception origins
without reopening source files or archives.

### Native and compiler provenance

`.gmod` mode/target come from the description, defaulting to auto/host; explicit
`--native` and `--native-target` override them independently. Ready ZIP modules
default to auto; legacy direct-source defaults are preserved. Build and execution
share one cleanup service. In auto, readiness failure does not produce duplicate
warnings when transitioning from build to execution; required fails. Death after
READY does not change the program result or permit removal beneath running native
code.

`--list-native-targets` presents only successfully probed targets, descriptions,
verified compilers and automatic selection. Failed probes and reasons appear
separately. At least one success yields status 0; no successful target yields a
nonzero status. `--cc` limits discovery to that compiler, inheriting the environment.
`--native-probe-timeout` is a positive integer number of seconds (default 10),
limits probes rather than user C compilation, and never enables unbounded waits.
Probes do not load the output library; results are cached only for this invocation.

Normal compilation records one `producers` entry containing compiler name, version
and a stable compiler-build identifier, never a packaging time, absolute path or
random identifier. Provenance is excluded from mainHash and does not replace
format/ABI checks.

CLI coverage lives in `scripts/check_module_cli.py`; existing source, standalone
binary, toolchain, native and cleanup checks remain. Actual test outcomes are
established by each change's CI, not by the existence of this document. Bounded
exit is implemented in #230 ([API](module-exit-api.md)); the [inspector](module-inspect-api.md)
and [merger](module-merge-api.md) are also implemented.
