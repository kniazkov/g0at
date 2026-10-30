# Сквозная приёмка поставки / End-to-end delivery acceptance (#219)

## Русский

Проверки используют пользовательские `goat build`, `goat run` и
`goat-inspect --json`, а не заменяют нативное исполнение проверкой строки target.
`check_module_delivery.py GOAT INSPECT CC` собирает модули отдельными процессами,
проверяет поставляемые архивы и запускает их после удаления исходников и исключения
C-компилятора из окружения. Результат VM сравнивается с auto/required; счётчики
`--save-native` подтверждают успешные нативные вызовы без fallback.
Эта же программа вызывается `check_package.py` для **перемещённых release-пакетов**,
поэтому тестируется установленный executable, а не оставшийся executable сборки.

Матрица существующего CI сохранена целиком: Linux x86-64 и AArch64 с GCC/Clang;
Windows MINGW32, MINGW64 и UCRT64. ARM64 использует настоящие ARM64 runners,
MINGW32 — настоящий 32-битный процесс. Обязательные проверки не пропускаются
и не используют `continue-on-error`.

### Переносимость и нижние границы

`check_module_delivery_requirements.py audit GOAT WRAPPER SELECTOR CC` проверяет
фактические аргументы компилятора всей нативной сборки и требования полученного
варианта. Уже существующие `integration.module_native` (CTest), `check_native_requirements.py`
и `check_native_toolchains.py` проверяют независимость архитектуры, CPU, runtime,
версии ОС и точной версии Goat ABI, сравнение версий по компонентам,
транзитивные требования библиотек и отказ при неизвестных требованиях.
Переменная `MSYSTEM` не определяет ABI уже собранного процесса. На Windows
реальные MINGW64/UCRT64 DLL собираются установленными отдельными компиляторами
и объединяются штатным merge; выбирается среда текущего процесса.
Явные профили `host+avx2` (x86-64) и `host+sha2` (AArch64) проверяются
по эффективным макросам компилятора и не выбираются базовым CPU-профилем.
`+sha2` требует SHA1/SHA2; неподдерживаемый `+crypto` не маскирует требование
PMULL отсутствующим полем метаданных.

На Linux `prepare-floor GOAT WRAPPER CC DEST` сохраняет контрольный модуль, собранный
на новом runner. Контрольный вариант содержит реальную дополнительную C-часть:
конструктор вызывает `__memcmpeq@GLIBC_2.35`, проверяет результат и записывает
маркер вызова ([добавление ABI в glibc](https://sourceware.org/pipermail/glibc-cvs/2021q4/075514.html)). Тестовая обёртка добавляет этот исходник в штатную компиляцию
вместе со всем runtime; метаданные не редактируются. Проверяются реальные
version requirements ELF, установленный производителем минимум и рост требований
относительно обычного варианта. Минимум контрольного варианта равен максимуму
обычного требования и 2.35; более высокое требование зависимостей не теряется.
В наблюдаемой матрице обычные варианты требовали 2.14 (x86-64 GCC), 2.17
(AArch64) или 2.36 (x86-64 Clang); это результаты конкретных toolchain, не константы
формата. `floor-image DEST` выбирает Ubuntu 22.04 для минимума 2.35 или Debian
bookworm для 2.36. Затем CI собирает только исполняющий Goat в выбранной среде
и вызывает `verify-floor GOAT DEST` с тем же архивом. Проверка требует **точного
совпадения** реальной glibc с заявленным минимумом, а не просто большей версии,
и подтверждает реальный native-вызов и маркер конструктора без исходников и
компилятора. Запуск обычных вариантов непосредственно на glibc 2.14/2.17 этой
проверкой не заявляется. Контейнер использует архитектуру runner, без эмуляции.
Ни 2.35, ни 2.36 не объявляются универсальным минимумом всех модулей: минимум
каждого варианта по-прежнему выводится из его бинарника и зависимостей. Минимум
вне проверенного набора сред завершает тест ошибкой, требуя пересмотра окружения.

**Контрольный запуск на Windows 10 пока не выполнен:** `windows-latest` не
является доказательством нижней границы. План отдельной проверки: предоставить
Windows 10 VM/runner с зафиксированными version/build; перенести три release-пакета
и готовые соответствующие `.gbin`; проверить MINGW32/MINGW64/UCRT64 при пустом
compiler PATH, native off/auto/required и положительных native-счётчиках; сохранить
версию ОС, требования архивов и отчёты. Вариант, требующий более новой сборки ОС,
должен быть отвергнут до загрузки. До появления такого окружения этот пункт
остаётся **не проверен**, а не «пройден» или скрыто пропущен в обязательном CI.

### Linux/musl после v1 (#248)

Два дополнительных задания `Native musl delivery` используют Alpine 3.22 в
Docker на настоящих x86-64 и AArch64 runners, без эмуляции. Они сохраняют
исходную матрицу glibc/Windows и выполняют Debug-сборку с основными тестами,
существующие проверки поставки и книги, затем Release-упаковку и `check_package.py`.

`scripts/check_module_musl.py produce GOAT SELECTOR FAMILY DEST` собирает одинаковый
исходник настоящими glibc- и musl-toolchain в соответствующих средах; SELECTOR —
`module_native_testing`, FAMILY — `glibc` или `musl`. Команда `verify` с теми же
аргументами проверяет сохранённые артефакты в каждой среде. Сравниваются переносимый
байткод и основной хеш; проверяются ABI 1, собственные хеши библиотек, базовые
CPU-требования и runtime. Версия musl в требованиях равна консервативной границе
по libc выбранного toolchain и подтверждается на исполняющем runtime этой версии.
Это не доказательство совместимости с более старой musl.

Автономные собственный, чужой и объединённый архивы исполняются в режимах
`off`/`auto`/`required` без исходников и доступного C-компилятора. Счётчики
подтверждают реальные native-вызовы и отсутствие скрытого fallback там, где
должен исполняться native. Чужой glibc-вариант не выбирается musl-процессом и
наоборот; `auto` использует VM, `required` сообщает отсутствие подходящего
варианта. Inspect успешно читает корректный чужой архив. Проверяются неизменность
поставленных байтов и идентичность основных частей после merge; требования
runtime не подменяются ручным редактированием метаданных.

### Существующие проверки остаются обязательными

- `check_module_hash.py`, `check_module_canonical.py`, CTest `integration.module_bytecode`,
  `check_module_writer.py` и format-vectors проверяют одни и те же закреплённые
  байты/основные SHA-256 на всех платформах, включая 32-битный процесс.
  Хеши различных DLL/SO совпадать не обязаны.
- `check_module_memory.py` использует управляемые ошибки выделения и ограничения
  размеров, в том числе в MINGW32, без гигабайтных выделений.
- `integration.module_consistency.*` (CTest), `check_module_validation.py` и
  `check_module_container.py` проверяют ссылки, сводки, дубликаты, целостность,
  CRC/размеры/сжатие до исполнения пользовательского кода.
- `integration.module_interface.*` (CTest) проверяет гарантии производителя и потребление
  сводок без исходников, сохраняя обязательную пользовательскую инициализацию.
  Link-time счётчики анализатора, абстрактного интерпретатора и VM с положительными
  контролями подтверждают нулевые вызовы при чтении метаданных; при компиляции
  потребителя анализируется только его собственный исходник.
- `check_module_merge.py` проверяет автономный merge, все-Deflate writer и
  сохранение идентичности; `check_module_inspect.py` — версию отчёта, коды
  диагностики и полный граф, включая различие skipped и errors.
- `check_module_library.py` проверяет согласованность встроенного descriptor,
  принадлежность и тип экспортируемых символов после реальной загрузки.

## English

`check_module_delivery.py GOAT INSPECT CC` exercises the public build, run and
JSON inspection commands in separate processes. Delivered archives run after
producer sources are removed and compiler discovery is disabled. VM results are
compared with auto/required, and `--save-native` counters prove successful native
calls rather than silent fallback. `check_package.py` also runs this suite using
**relocated release tools**, not the build-tree executable.

The complete existing matrix remains mandatory: Linux x86-64/AArch64 GCC/Clang
and Windows MINGW32/MINGW64/UCRT64. ARM64 runs on native hardware and MINGW32
uses a real 32-bit process. No mandatory checks use skips or continue-on-error.

### Portability and lower bounds

`check_module_delivery_requirements.py audit GOAT WRAPPER SELECTOR CC` audits
actual compiler arguments for the complete native build and tests the resulting
variant requirements. Existing native selector, requirements and toolchain suites
retain independent CPU, architecture, OS, runtime and exact Goat ABI checks,
component-wise version comparison, transitive dependency requirements and
fail-closed handling of unknown requirements. Changing MSYSTEM cannot change
an already-built process ABI. Real MINGW64/UCRT64 DLLs are built with separate
installed compilers and combined with the standard merge tool; selection follows
the running process. Explicit host+avx2 (x86-64) and host+sha2 (AArch64) profiles
are audited using effective compiler macros and rejected by baseline-only CPU
profiles. SHA1/SHA2 are modeled explicitly; unsupported +crypto does not hide an
unmodeled PMULL requirement.

Linux `prepare-floor GOAT WRAPPER CC DEST` preserves a native module produced on the
newer runner. Its real additional C component calls `__memcmpeq@GLIBC_2.35`
from a constructor, checks the result and writes an execution marker
([glibc ABI introduction](https://sourceware.org/pipermail/glibc-cvs/2021q4/075514.html)). A test-only
compiler wrapper adds this source to the ordinary whole-runtime build; metadata
is never rewritten. ELF version requirements, producer-derived minimum and the
increase over the ordinary variant are checked: the control minimum is the
maximum of the ordinary requirement and 2.35, never discarding a higher dependency
requirement. Observed ordinary values were 2.14 (x86-64 GCC), 2.17 (AArch64) and
2.36 (x86-64 Clang), not format constants. `floor-image DEST` selects Ubuntu 22.04
for minimum 2.35 or Debian bookworm for 2.36. CI builds the consuming Goat in the
selected userspace, then runs
`verify-floor GOAT DEST` against that unchanged module. Verification demands
**exact equality** between actual glibc and the declared minimum, and proves
native calls and the constructor marker without sources or compiler access.
This does not claim execution of ordinary variants directly on glibc 2.14/2.17.
Both runner architectures run natively, without emulation. Neither 2.35 nor 2.36
is a universal v1 minimum. Each variant's minimum remains derived from its image
and dependency closure. A minimum outside the controlled environment set fails
the test rather than silently claiming that a newer runtime proves the minimum.

**Windows 10 execution remains unverified.** windows-latest is not evidence for
the lower boundary. The separate validation plan requires a Windows 10 VM/runner
with recorded version/build, the three relocated release distributions and their
ready modules. Run MINGW32/MINGW64/UCRT64 with compiler discovery disabled in
off/auto/required modes, preserve positive native-call counters and OS/module
requirements, and reject variants requiring a newer Windows build before loading.
Until that environment is available, this is explicitly **not run**, never passed.

### Post-v1 Linux/musl (#248)

Two additional `Native musl delivery` jobs run Alpine 3.22 in Docker on native
x86-64 and AArch64 runners, without emulation. They preserve the original
glibc/Windows matrix, run Debug builds and core tests, the existing delivery and
book suites, then Release packaging and `check_package.py`.

`scripts/check_module_musl.py produce GOAT SELECTOR FAMILY DEST` builds identical
sources using real glibc and musl toolchains in their respective environments.
SELECTOR is `module_native_testing`; FAMILY is `glibc` or `musl`. The `verify`
command with the same arguments checks the preserved artifacts in each environment.
It compares portable bytecode and mainHash, checks ABI 1, per-library hashes,
baseline CPU requirements and runtime. The musl requirement is a conservative
floor taken from the selected toolchain's libc, verified on that executing runtime
version. It does not prove compatibility with an older musl.

Autonomous own-family, foreign-family and merged archives run in off/auto/required
modes without sources or an available C compiler. Counters prove actual native
calls and exclude silent fallback where native execution is expected. A glibc
variant is not selected by a musl process or vice versa: auto uses the VM and
required reports no suitable variant. Inspect successfully reads a valid foreign
archive. Delivered bytes remain unchanged, and merge preserves the main-part
identity; runtime requirements are not manufactured by relabeling metadata.

Existing mandatory suites provide frozen cross-platform bytecode/main-hash and
writer vectors (`check_module_hash`, `canonical`, `bytecode`, `writer`), bounded
allocation failures (`memory`), structural/integrity/compression checks
(`consistency`, `validation`, `container`), source-free producer/consumer summary
contracts (`interface`), autonomous merge (`merge`), stable versioned JSON reports
(`inspect`), and loaded descriptor/symbol ownership (`library`). These tests are
reused rather than replaced with a second implementation of the format or loader.
Link-time analyzer, abstract-interpreter and VM counters have positive controls:
metadata-only graph reading calls none; source-free consumer compilation analyzes
only its own source. Native image hashes are intentionally independent across platforms.
