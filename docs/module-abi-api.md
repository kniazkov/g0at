# ABI модулей / Module calling ABI

## Русский

Этап #209 реализован отдельно от старого `goat_native_query_v2`:
`include/goat/module_abi.h` задаёт точный ABI `goat.adapter.v1` для готовых модулей.
Номер ABI равен 1 и не выводится из версии выпуска Goat. Старые структуры не
приводятся к новым: в частности, строковая длина теперь uint64, а значение и
строковый вид имеют размер/выравнивание 16/8 даже в MINGW32. Экспортируемые
`goat_fn_<specializationId>_v1` используют C linkage и `__cdecl` на Windows/x86.

`src/module/module_abi.h` предоставляет:

- `goat_module_descriptor_read`: ограниченный снимок `GOATND01`, длина uint64 LE,
  строгий канонический `native.schema.json` и завершающий ноль вне длины. Нет
  strlen, указателей внутри описания и загрузки DLL/SO. Переполнение, обрезание,
  лишние байты, неизвестная схема/ABI и неканоническое представление отвергаются.
- `goat_module_abi_validate`: проверка точной версии интерфейса и специализаций
  уже проверенного схемой варианта. Символ точно соответствует specializationId;
  идентификаторы уникальны внутри варианта. Для одной functionId пересекающиеся
  условия отвергаются. Разные модули проверяются отдельно, числовой ID не создаёт
  глобальной привязки. Связи с FUNC/байткодом проверяет [этап #233](module-consistency-api.md).
- `goat_module_value_valid`: проверка tag/reserved/payload, нулей в padding,
  Unicode scalars и размеров строк. Перед разыменованием строк вызывается
  предоставленная хостом проверка читаемого диапазона. Этот callback не является
  пользовательским кодом языка. Память остаётся стабильной до копирования результата.
- `goat_module_abi_invoke`: один вызов адаптера после проверки контекста и аргументов.
  Результат предварительно обнулён; проверяются статус, тип и представление.
  Любой ненулевой статус (включая неизвестный) после входа — ошибка, не разрешение
  повторить действие в VM. На ошибке результат снова нулевой. Строки заимствованы:
  хост копирует их до освобождения аргументов/контекста. Буферы результата и
  аргументов должны быть отдельными. Хостовые таблицы и массив аргументов являются
  доверенными доступными объектами; размеры этих объектов обеспечивает вызывающий.

`module_guards.h` проверяет декларативные условия без исполнения Goat-кода,
перегрузок, callback и обращений к свойствам. Отсутствующий фактический аргумент
считается null; лишние уже вычисленные аргументы не добавляют формальных слотов.
Типы точные, без преобразований. Строки сравниваются целиком с явной длиной,
включая нули; ±0 равны, NaN следует IEEE. Проверка пересечений использует свидетелей
по всем скалярным доменам: границы и соседние представимые значения, отдельный NaN,
null/boolean и строковые равенства/исключения. Невыполнимые условия допустимы, но
никогда не выбираются, как в эталоне формата. При отсутствии совпадения функция
проверки возвращает false, не вызывает ни native, ни VM.

Новый ABI не даёт доступ к VM-объектам. Услуги памяти выполняются хостом с тем же
opaque state; только внешний владелец делает bulk cleanup. Unicode-услуги используют
17.0.0; полное изменение регистра не сводится к циклу простых scalar callbacks.
Exit не является самостоятельным C exit: общий механизм завершения и его
интеграционные проверки остаются #207/#230. Неподдержанные операции не становятся
нативными только из-за наличия подходящей скалярной сигнатуры.

### Границы и проверки

Читатель описания принимает **уже безопасно скопированный** буфер. Это не проверка
произвольного адреса из OS loader: принадлежность всех mapped ranges загруженной
библиотеке проверяет загрузчик #210 до копирования. Сравнение встроенного описания
с выбранной записью архива выполняет #233. Runtime-выбор всех применимых
специализаций, защита от неоднозначности при вызове, преобразование VM-аргументов,
сохранение/потеря привязки clone/flat — #232. Эта задача не объявляет их готовыми.

`python3 scripts/check_module_abi.py build/<config>/module_abi_testing` проверяет
описания, повреждённые значения, границы условий и реальную библиотеку с экспортом
описания и адаптера. Вызов проходит через новый ABI, а не старый query ABI или VM
fallback. Существующая матрица Linux GCC/Clang x86-64/ARM64 и Windows
MINGW32/MINGW64/UCRT64 запускает тот же набор. Сборка тестовой библиотеки не требует
готовой интеграции .gmod (#211); полноценная поставка модуля проверяется в #219.

## English

Step #209 adds `include/goat/module_abi.h`, the exact `goat.adapter.v1` module
boundary, independently of the legacy `goat_native_query_v2` ABI. ABI 1 is not a
Goat release number. Never reinterpret legacy structures: values and string views
are 16-byte, 8-aligned objects on all targets, including MINGW32; string lengths
are uint64. Export `goat_fn_<specializationId>_v1` with C linkage and Windows/x86
`__cdecl`.

`src/module/module_abi.h` provides bounded canonical `GOATND01` snapshot parsing,
strict native JSON schema support, exact ABI/symbol validation and checked one-shot
calls. Truncation, overflow, trailing bytes, a missing terminal zero, unsupported
versions and noncanonical JSON are errors. Specialization IDs are unique within
a variant; overlapping guards for one function are rejected. IDs remain local
to each independently validated module, not a global binding key. #233 now checks links to FUNC/bytecode and comparison with the archive's
selected descriptor; see [consistency API](module-consistency-api.md).

Value checks cover tags, reserved fields, padding, canonical payloads, Unicode
scalars and overflow-safe explicit string lengths. A host readable-range callback
is required before dereferencing string pointers; this is not a Goat callback.
The memory must remain stable through validation and consumption. Trusted host
service tables and argument arrays must be accessible objects of the stated size;
output and argument buffers must be separate. The host controls their lifetime.

Invocation initializes the output to zero, calls once, then checks status and
expected type/representation. Every nonzero or unknown status after entry is a
failure, never permission to replay effects on the VM. Failed results are cleared.
Strings are borrowed and copied before outer-call cleanup. Allocation uses host
services and the same opaque state; only the outer owner performs bulk cleanup.
Unicode services use 17.0.0; full string case mapping needs the pinned algorithm,
not just a loop of simple scalar callbacks. Exit uses the common Goat mechanism
in #207/#230, not independent C termination. Unsupported operations do not become
native merely because their signatures contain scalar types.

`module_guards.h` performs no user code, overload, callback or property access.
It uses exact types; missing arguments are null and extra already-evaluated
arguments add no formal slots. String comparison includes embedded zeros. Signed
zeros compare equal and NaNs follow IEEE. Overlap is decided using witnesses in
all scalar domains: endpoints/neighbors, NaN, finite null/boolean domains and
string equality/exclusion. Unsatisfiable guards remain valid but never match,
as in the reference format. A mismatch returns false without invoking VM/native.

The descriptor parser accepts an **already safely copied snapshot**. It is not a
probe of arbitrary OS memory: #210 verifies mapped ranges belong to the loaded
library before copying. #233 compares embedded and archive metadata. Runtime
ambiguity checks, VM argument conversion and clone/flat binding retention belong
to #232, not this interface step. The old runtime remains separate.

Run `python3 scripts/check_module_abi.py build/<config>/module_abi_testing` for
malformed envelopes/values, guard boundaries and a real shared library exporting
the descriptor and fixed adapter. Actual calls use the new ABI, not VM fallback
or the legacy query ABI. Every existing Linux GCC/Clang x86-64/ARM64 and Windows
MINGW32/MINGW64/UCRT64 job runs these tests. Controlled library compilation does
not wait for .gmod integration #211; the complete packaged pipeline remains #219.
