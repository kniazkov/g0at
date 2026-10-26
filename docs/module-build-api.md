# Нативная сборка модулей / Native module builds

## Русский

`build_module` (`common/module_build.h`) принимает неизменный граф исходников,
прямые зависимости и проверенный граф модулей. Он возвращает байткод, метаданные
и не более одного нативного варианта. `module_build_writer_input` передаёт их
общему writer; до успешного завершения никакой пользовательский файл не публикуется.
Инициализаторы Goat не выполняются, DLL/SO не загружается средствами ОС.
Перед упаковкой статически проверяются реальные экспортируемые descriptor и adapter-символы
ELF/PE: успешный exit компилятора с чужой библиотекой не считается успешной сборкой.

Отсутствующие настройки берутся из `.gmod`, затем используются `auto` и `host`.
Явные параметры API переопределяют режим и цель независимо. `off` не запускает
компилятор. `auto` без пригодного компилятора для `host` возвращает bytecode-only
и одно предупреждение. `required` требует собственной специализации, включая
включённые файлы; native внешних зависимостей не засчитывается. Неподдерживаемая
или недоступная явная цель не заменяется host. Ошибка сборки пользовательского C
уже выбранным компилятором фатальна: другие компиляторы не перебираются.

`native_toolchain.h` реализует обнаружение через PATH, без сканирования диска.
Обычный поиск останавливается на первом успешном кандидате; перечисление проверяет
всех кандидатов и возвращает успешные цели отдельно от диагностики отказов.
Приоритет: Linux `cc`, `gcc`, `clang`; Windows `gcc`, `clang`, `cc`.
Затем рассматриваются PATH-кандидаты `x86_64-linux-gnu-gcc`,
`aarch64-linux-gnu-gcc`, `x86_64-w64-mingw32-gcc`, `i686-w64-mingw32-gcc`.
Явный `--cc`/параметр compiler ограничивает поиск одним исполняемым файлом.
Прямой `.goat` и `--save-library` сохраняют прежний `CC` как выбор компилятора;
явный `--cc` имеет приоритет. Модульный API получает compiler явно от вызывающего CLI.
Окружение наследуется. Проба компилирует и линкует DLL/SO, проверяет фактический
формат и требования, но никогда не загружает результат. Общий монотонный бюджет
пробы по умолчанию 10 секунд включает запросы сведений; сборку программы он
не ограничивает. Успехи, отказы и таймауты кешируются только внутри контекста
текущего запуска, с ключом compiler/target/timeout; окружение в этом контексте
должно оставаться неизменным.

Поддерживаемые backend-цели: `linux-x86_64-gnu`, `linux-aarch64-gnu`,
`windows-x86_64-mingw64`, `windows-x86_64-ucrt64`, `windows-x86-mingw32`.
Наличие имени цели не означает доступности toolchain. `host` выбирает профиль
исполняющего процесса, а не переменную MSYSTEM. Базы: x86-64/SSE2,
Armv8-A/FP/NEON, x86/i686. Явный суффикс `+avx2` для x86-64 повышает CPU-требования;
прочие неподдерживаемые профили отклоняются. Все исходники встроенного runtime
перекомпилируются теми же целевыми флагами: host-архив не переносится в чужую цель.

Инспектор читает ELF и замыкание динамических зависимостей через выбранный
компилятор, учитывает version requirements и GNU ABI notes. Минимальная glibc
не копируется с build-host. Неизвестные зависимости/требования отклоняются.
PE проверяет разрядность, runtime и разрешённый профиль импортов Windows 10;
неизвестные DLL, символы, ordinal/delay imports не дают ложного обещания
совместимости. Дескриптор генерируется повторно после установления полных
требований; повторная сборка обязана подтвердить тот же target. `cCompiler`
записывает фактически выбранные имя и версию. Основной хеш и bytecode не зависят
от наличия native; библиотека имеет собственный SHA-256.

Встраивающий executable вызывает `cleanup_service_main` до обычного разбора
аргументов либо явно задаёт executable уборщика при создании контекста.
Один `native_build_context_t` разделяет общий `cleanup_service_t` с пробами,
модульной и прямой `.goat` компиляцией. READY подтверждается до создания файлов
сборки; процессы и потомки запускаются только через `run_cleanup_process`.
Временная библиотека удерживает контекст до последнего освобождения после unload.
Пользовательские сохранённые артефакты находятся вне дерева уборщика.
Неудача READY допускает auto→VM, но required завершается до пользовательского кода.
Гибель уже готового уборщика не меняет код завершения выполняющейся программы;
небезопасной ручной уборки из-под работающих процессов нет.

Проверки: `check_native_artifact.py`, `check_native_toolchains.py`, `check_native_requirements.py`,
`check_native_cleanup.py`, `check_module_build.py`, `check_module_native_values.py`.
Существующие native/unit/runtime suites сохраняются. Нижняя версия Windows 10
и старые glibc требуют отдельного контрольного окружения: CI на текущем runner
не объявляется проверкой запуска на этих нижних границах. Представление
`--list-native-targets` и полноценный `.gmod` CLI остаются #191; OS loader — #210.

## English

`build_module` in `common/module_build.h` consumes frozen sources, direct bindings
and the validated module graph. It produces bytecode, metadata and at most one
native variant. `module_build_writer_input` feeds the existing transactional writer.
No user artifact is published on failure; no Goat initializer runs and no native
library is loaded during production. Before packaging, static ELF/PE export inspection
checks the actual descriptor and adapter symbols: a compiler success with a substituted
unrelated library is rejected.

Optional API overrides independently replace `.gmod` mode/target; missing values
use auto/host. Off invokes no compiler. Auto without a usable host compiler returns
bytecode-only with one warning. Required needs an own specialization, including
own included sources, never merely a dependency's native implementation. An explicit
unsupported/unavailable target is not replaced by host. Generated user C compilation
failure after compiler selection is fatal, without alternate-compiler retries.

Discovery searches PATH only, in stable order: Linux cc/gcc/clang; Windows
gcc/clang/cc, followed by x86_64-linux-gnu-gcc, aarch64-linux-gnu-gcc,
x86_64-w64-mingw32-gcc and i686-w64-mingw32-gcc. Explicit --cc/compiler restricts both selection and enumeration to
one executable, with inherited environment. Direct .goat/--save-library retain legacy
CC selection, overridden by --cc; the module API receives compiler selection from its caller. Ordinary selection stops at the first
successful probe; enumeration retains all successes and separate failure reasons.
A probe compiles and links a shared library, checks the resulting target and
requirements, but never loads it. The default 10-second monotonic budget covers
all query/probe commands, not generated user C builds. In-memory results, including
failures/timeouts, are keyed by compiler, target and timeout; they are not persisted.
The environment must remain unchanged within a context.

Backends support linux-x86_64-gnu, linux-aarch64-gnu, windows-x86_64-mingw64,
windows-x86_64-ucrt64 and windows-x86-mingw32. A recognized target is not a promise
of an installed toolchain. Host derives the running process profile, not MSYSTEM.
Baselines are x86-64/SSE2, Armv8-A/FP/NEON and x86/i686. An explicit +avx2 x86-64
profile raises CPU requirements; unsupported profiles fail closed. Bundled runtime
sources are rebuilt with the same target flags instead of linking a host-only archive.

ELF inspection follows compiler-resolved dynamic dependencies and combines their
version requirements and GNU ABI notes, without substituting build-host glibc.
Unknown requirements/dependencies fail. PE validates architecture, runtime and the
supported Windows 10 import profile; unknown DLLs/symbols/ordinal/delay imports fail
closed. After inspecting full linked requirements the descriptor may be re-emitted;
the final build must reproduce the same target. cCompiler records the actual selected
compiler. Native presence does not change mainHash or bytecode; the complete library
has its own SHA-256.

An embedding executable must dispatch cleanup_service_main before ordinary argument
parsing, or explicitly provide a cleaner-host executable when creating its context.
A shared reference-counted native_build_context owns the common cleanup service for
probes, module builds and direct .goat compilation. READY precedes temporary build
files. Managed compiler trees run through run_cleanup_process. Loaded temporary
libraries retain the context through unload; saved artifacts stay outside cleanup.
Pre-READY failure permits auto VM fallback, but required stops before user code.
Death after READY does not change the running program's result and does not authorize
unsafe manual removal while processes are still writing.

The six native-artifact/toolchain/requirements/cleanup/module-build/module-native-values
scripts run in CI alongside existing suites. Current runners do not establish actual
execution on minimum Windows 10/older-glibc systems; those require dedicated validation.
The full .gmod CLI and list-native-targets presentation remain #191; OS loading is #210.

## Граница символов ABI v1 / ABI v1 character boundary

Goat допускает полное значение uint32 у символа, тогда как ABI модулей v1 передаёт
только Unicode scalar values. Поэтому модульный backend не выпускает специализацию
с непроверенным вызовом chr и исключает зависящих от неё нативных вызывающих.
Доказанная скалярная константа допустима; некорректные константы символов и строк
также исключаются. Семантика VM и прежнего прямого native-запуска не меняется.
Если после фильтрации не остаётся собственных специализаций, required завершается
до пользовательского кода. Обычные специализации основаны на обобщённых типах
аргументов, а не на наблюдённых значениях: например, наблюдение square(7) разрешает
тот же адаптер для square(9).

Goat character values span uint32, whereas module ABI v1 carries Unicode scalars.
The module backend therefore omits unproven chr constructions and dependent native
callers. A proven scalar constant is allowed; invalid character/string constants
are omitted too. VM and legacy direct-native semantics remain unchanged. If no own
specialization remains, required fails before user code. Ordinary specializations
use type-wide argument summaries, not observed values: observing square(7) also
permits the same adapter for square(9).
