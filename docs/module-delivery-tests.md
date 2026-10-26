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
варианта. Уже существующие `check_module_native.py`, `check_native_requirements.py`
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
относительно обычного варианта (на Ubuntu 24.04 это 2.14 для x86-64 и 2.17 для
AArch64). Затем CI собирает только исполняющий Goat внутри Ubuntu 22.04
и вызывает `verify-floor GOAT DEST` с тем же архивом. Проверка требует **точного
совпадения** реальной glibc с заявленным минимумом, а не просто большей версии,
и подтверждает реальный native-вызов и маркер конструктора без исходников и
компилятора. Запуск обычных вариантов непосредственно на glibc 2.14/2.17 этой
проверкой не заявляется. Контейнер
использует архитектуру runner, без эмуляции. Сейчас контрольное окружение имеет
glibc 2.35. Это не объявляет 2.35 универсальным минимумом всех модулей: минимум
каждого варианта по-прежнему выводится из его бинарника и зависимостей. Если
требования изменятся, тест завершится ошибкой, требуя пересмотра окружения.

**Контрольный запуск на Windows 10 пока не выполнен:** `windows-latest` не
является доказательством нижней границы. План отдельной проверки: предоставить
Windows 10 VM/runner с зафиксированными version/build; перенести три release-пакета
и готовые соответствующие `.gbin`; проверить MINGW32/MINGW64/UCRT64 при пустом
compiler PATH, native off/auto/required и положительных native-счётчиках; сохранить
версию ОС, требования архивов и отчёты. Вариант, требующий более новой сборки ОС,
должен быть отвергнут до загрузки. До появления такого окружения этот пункт
остаётся **не проверен**, а не «пройден» или скрыто пропущен в обязательном CI.

### Существующие проверки остаются обязательными

- `check_module_hash.py`, `check_module_canonical.py`, `check_module_bytecode.py`,
  `check_module_writer.py` и format-vectors проверяют одни и те же закреплённые
  байты/основные SHA-256 на всех платформах, включая 32-битный процесс.
  Хеши различных DLL/SO совпадать не обязаны.
- `check_module_memory.py` использует управляемые ошибки выделения и ограничения
  размеров, в том числе в MINGW32, без гигабайтных выделений.
- `check_module_consistency.py`, `check_module_validation.py` и
  `check_module_container.py` проверяют ссылки, сводки, дубликаты, целостность,
  CRC/размеры/сжатие до исполнения пользовательского кода.
- `check_module_interface.py` проверяет гарантии производителя и потребление
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
increase over the ordinary variant are checked (ordinary Ubuntu 24.04 variants
require 2.14 on x86-64 / 2.17 on AArch64). CI builds the consuming Goat inside
Ubuntu 22.04, then runs
`verify-floor GOAT DEST` against that unchanged module. Verification demands
**exact equality** between actual glibc and the declared minimum, and proves
native calls and the constructor marker without sources or compiler access.
This does not claim execution of ordinary variants directly on glibc 2.14/2.17.
Both runner architectures run
natively, without emulation. The current controlled runtime is glibc 2.35;
this is not a universal v1 minimum. Each variant's minimum remains derived from
its image and dependency closure. Changed requirements fail the test rather
than silently claiming that a newer runtime proves the minimum.

**Windows 10 execution remains unverified.** windows-latest is not evidence for
the lower boundary. The separate validation plan requires a Windows 10 VM/runner
with recorded version/build, the three relocated release distributions and their
ready modules. Run MINGW32/MINGW64/UCRT64 with compiler discovery disabled in
off/auto/required modes, preserve positive native-call counters and OS/module
requirements, and reject variants requiring a newer Windows build before loading.
Until that environment is available, this is explicitly **not run**, never passed.

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
