# Нативные метаданные / Native platform metadata

## Русский

`src/module/module_native.h` реализует этап #208, не загружая DLL/SO и не исполняя
модуль. Вход `goat_module_native_select` — готовый `goat_module_container`, среда
процесса и список **реально реализованных вызывающим загрузчиком** версий ABI.
Контейнер уже проверил строгую схему и SHA-256 **каждой** библиотеки, включая
несовместимые платформы. Неправильный хеш не превращается в отсутствие подходящего
варианта. Выбор не заменяет проверку байткода, встроенного описания или ABI вызова.

Успех возвращает индекс единственного варианта в нормализованном `metadata.native`
(тот же порядок использует `goat_module_container_native_at`). Ноль подходящих
вариантов — `GOAT_NATIVE_NO_MATCH`; два и более — `GOAT_NATIVE_AMBIGUOUS`, без
ранжирования по скорости, версии или порядку архива. Ошибка противоречивых
платформенных требований — `GOAT_NATIVE_INVALID_METADATA`. Все записи проверяются
до результата, даже после обнаружения неоднозначности. При любом неуспехе выходной
индекс равен `SIZE_MAX`; ошибка содержит индекс записи, когда он известен.

Все требования соединены логическим И:

- ОС, архитектура **процесса**, разрядность и little-endian совпадают точно.
  x86/MINGW32 не выбирает x86-64 DLL даже на 64-битной Windows.
- Linux использует glibc либо musl, Windows — msvcrt либо ucrt. Семейства не
  взаимозаменяемы. Чужой известный runtime не портит корректный контейнер.
- Минимальные версии ОС/runtime сравниваются численно по четырём uint32
  компонентам с дополнением нулями: `[2,9] < [2,28]`, `[2,28,0] == [2,28]`.
  Linux сравнивает версию ядра; Windows — major/minor/build/revision, минимум 10.
  Неизвестная доступная версия не удовлетворяет положительному требованию.
  Runtime `[0]` по контракту означает отсутствие дополнительного ограничения
  версии, но **не** отменяет точное совпадение семейства.
- Goat ABI совпадает точно с элементом переданного списка; числового сравнения
  «не ниже» нет. Пустой список не допускает ни одного нативного варианта.
- Неизвестное непустое имя CPU-возможности исключает вариант. Известная возможность
  другой архитектуры и суффикс библиотеки, противоречащий ОС (`.so`/`.dll`), дают
  ошибку метаданных, в том числе у чужого варианта.

База архитектуры и предпосылки проверяются и при отсутствии их явного повторения
в списке: x86 требует i686, x86-64 SSE2, AArch64 FP+NEON. AVX2/FMA/AVX512F требуют
AVX, AVX — SSE2; SSE4.2 → SSE4.1 → SSSE3 → SSE3 → SSE2. На x86 AES/SHA1/SHA2
требуют SSE2, на AArch64 SVE2 требует SVE. Это не дописывает поля в JSON и не меняет
канонические байты/хеш. Производитель обязан описывать фактические требования,
включая предпосылки; пропуск предпосылки никогда не ослабляет проверку.

### Достоверная среда и профили

`goat_native_environment_detect` узнаёт архитектуру и runtime текущего процесса,
а не установленной ОС/компилятора. `MSYSTEM`, `PATH`, `CC` не выбирают runtime.
Linux: `uname`, `gnu_get_libc_version`; AArch64: OS HWCAP; x86: CPUID и, для AVX,
проверки XSAVE/OSXSAVE/XGETBV. Одного CPU-бита AVX недостаточно. Windows: реальные
версии из `RtlGetVersion` и ресурса уже загруженной CRT, семейство из параметров
компиляции Goat. Посторонние библиотеки при определении среды не загружаются.

Недоступные сведения остаются неизвестными. Windows servicing revision, не
возвращаемая `RtlGetVersion`, консервативно равна нулю: вариант с большим revision
не будет выбран даже на фактически подходящей системе. Это безопасная нижняя
оценка, не обещание поддержки любой сборки Windows 10. Backend musl отложен до
#248; отсутствие glibc не выдаётся за доказанную musl.

`goat_native_environment_read` читает строгий профиль `target.schema.json`; это
те же поля, но версии и CPU-возможности означают **доступные**, а не требуемые.
Неизвестные имена возможностей не дают известных возможностей. Профиль может
моделировать отсутствующий SIMD/AVX; выбор всё равно проверяет предпосылки.
`GOAT_MODULE_TARGET` также доступен через общий JSON reader/writer. Неправильные
типы, повторные/неизвестные поля, несогласованные ОС/runtime/разрядность отвергаются.
Синтетический профиль не разрешает пробную загрузку на другом хосте.

`cCompiler` остаётся закрытым информационным объектом `name`/`version`. Его
отсутствие допустимо; строки не обрезаются и не меняют регистр. Он не участвует
в выборе или основном хеше. Сборка записывает фактические сведения в #211,
показ/перенос и разрешение конфликтов слияния выполняются в #212/#215.

### Проверки и границы

`python3 scripts/check_module_native.py build/<config>/module_native_testing`
проверяет искусственные среды, минимумы, семейства CRT, все архитектуры, неизвестные
требования, неоднозначность, ABI, хеши чужих вариантов и неизменность поведения от
подмены окружения. Дополнительно компилируется контрольная библиотека с явной
базой `-march=x86-64`, `-march=armv8-a` или `-march=i686`; проверяются эффективные
макросы CPU, её хеш, выбор и **реальный вызов** с результатом 42 без VM fallback.
Проверка запускается на всех существующих Linux GCC/Clang x86-64/ARM64 и Windows
MINGW32/MINGW64/UCRT64 jobs. Тестовая функция не выдаётся за реализацию нового ABI
специализаций: это отдельная задача #209.

Определение требований результата линковки и всех зависимостей, контроль флагов
всей нативной сборки — #211. Общий loader/режимы — #210/#185; встроенные описания —
#209/#233; полная цепочка нового модульного ABI — #219. Контрольный запуск на
заявленном минимуме glibc/Windows 10 остаётся частью #211/#219: современный runner
не доказывает нижнюю границу. Здесь она проверяется сравнением заданных профилей,
а не объявляется пройденной реальной проверкой старой ОС.

## English

`src/module/module_native.h` implements #208 without loading or executing a module.
`goat_module_native_select` takes a validated `goat_module_container`, the process
environment and the ABI versions **actually implemented by its caller**. The
container has already validated every library's SHA-256, including foreign
variants. A bad hash is corruption, not native unavailability. Selection does
not replace bytecode, embedded-descriptor or calling-ABI validation.

Success returns the unique variant's normalized `metadata.native` index (also
used by `goat_module_container_native_at`). No match returns `GOAT_NATIVE_NO_MATCH`;
multiple matches return `GOAT_NATIVE_AMBIGUOUS`, with no speed/version/order
preference. Contradictory platform requirements return `GOAT_NATIVE_INVALID_METADATA`.
Every record is checked, even after ambiguity is discovered. Failure sets the
output index to `SIZE_MAX`; error details identify a record when applicable.

Requirements are conjunctive: exact OS, **process** architecture, bitness,
little-endian, runtime family and explicitly supported Goat ABI; numeric
zero-extended four-uint32 minimum OS/runtime versions; and usable CPU features.
`[2,9] < [2,28]`, `[2,28,0] == [2,28]`. Linux compares kernel versions; Windows
major/minor/build/revision has a minimum of 10. Unknown available versions do not
satisfy positive requirements. Runtime `[0]` means no additional version floor,
not unspecified runtime family. glibc/musl and msvcrt/ucrt never substitute for
each other. A correct foreign-runtime variant remains a correct container member.
A 32-bit process on 64-bit Windows cannot select a 64-bit DLL. ABI comparison is
exact membership, never numeric seniority; an empty supported list permits none.

Unknown nonempty CPU names exclude variants. Known features of the wrong
architecture or a library suffix inconsistent with OS (`.so`/`.dll`) are metadata
errors, also for foreign variants. Architecture baselines and prerequisites are
checked even if not repeated explicitly: x86 requires i686, x86-64 SSE2, AArch64
FP+NEON; AVX2/FMA/AVX512F require AVX and AVX requires SSE2; SSE4.2 → SSE4.1 → SSSE3
→ SSE3 → SSE2; x86 AES/SHA1/SHA2 require SSE2; AArch64 SVE2 requires SVE. This does
not rewrite JSON or change canonical bytes/hashes. Producers must record actual
requirements including prerequisites; omitting one never weakens checking.

### Actual environment and profiles

`goat_native_environment_detect` identifies the running process, not installed
compilers or the OS bitness. `MSYSTEM`, `PATH` and `CC` do not select its runtime.
Linux uses `uname`/`gnu_get_libc_version`, ARM64 OS HWCAP, and x86 CPUID with guarded
XSAVE/OSXSAVE/XGETBV for AVX register state. Windows uses `RtlGetVersion`, compile-time
CRT family and version resources from the already-loaded CRT. No foreign library
is loaded. Unknown facts stay unknown; musl backend detection is deferred to #248,
not guessed from the absence of glibc.

Windows servicing revision unavailable through `RtlGetVersion` conservatively
uses zero. A higher-revision variant is rejected even if the machine actually
supports it: this is a safe lower bound, not universal Windows 10 compatibility.
`goat_native_environment_read` parses the strict `target.schema.json` profile,
where version/CPU fields describe available capabilities. Unknown feature names
confer no known capability. Profiles can model missing SIMD/AVX capabilities;
selection still checks prerequisites. `GOAT_MODULE_TARGET` is also supported by
the shared JSON reader/writer. Profiles do not authorize foreign-host load probes.

The closed optional `cCompiler` object retains exact decoded `name`/`version`
strings and affects neither selection nor the main hash. Actual producer data,
inspect display and merge conflict handling belong to #211/#212/#215.

### Tests and boundaries

Run `python3 scripts/check_module_native.py build/<config>/module_native_testing`.
Synthetic profiles cover all architectures, runtime families, numeric minimums,
unknown requirements, ambiguity, ABI membership, foreign-variant hashes and
environment spoofing. A real control library is compiled with explicit baseline
`-march=x86-64`, `-march=armv8-a` or `-march=i686`; effective CPU macros, library
hash/selection and an actual native call returning 42 are checked without VM
fallback. Every existing Linux GCC/Clang x86-64/ARM64 and Windows
MINGW32/MINGW64/UCRT64 job runs it. This control call is not the new specialization
ABI, which belongs to #209.

Complete linked-result/dependency requirement discovery and whole-build flag
control remain #211; general loading/modes #210/#185; embedded descriptors
#209/#233; the complete module ABI pipeline #219. Real execution at the stated
minimum glibc/Windows 10 remains #211/#219: a modern runner is not evidence of old
OS compatibility. This step tests minimum comparison using profiles, and does
not mark old-OS execution as passed.
