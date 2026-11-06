# Нативные функциональные тесты / Native functional tests

```sh
ctest --test-dir build -R '^integration.native_functional\.' --output-on-failure
build/native_runner_testing all build/goat build/native_runner_policy_testing . build test/data --only quadratic string_chain
```

На Windows используй `build/goat.exe` и C runners с расширением `.exe`, с MinGW GCC в PATH.
CTest использует настроенные пути; для прямого запуска укажи пути к C runner, Goat, selftest, корню репозитория, сборке и данным.

`list.json` — общий список. Новый обычный тест добавляется без правки CI:

1. Создай `test/data/native/<name>/program.goat`.
2. Добавь запись `{"name": "<name>", "calls": 1}` в `list.json`.
3. При необходимости добавь `expected_output.txt` с точным UTF-8-выводом.

`calls` — число входов из VM в нативный адаптер; вложенные вызовы внутри нативного
кода не увеличивают этот счётчик. Оно обязательно положительное. Опциональное
`bound` задаёт точное число привязанных Goat-функций, а не специализаций.
Несколько сигнатур одной функции дают `bound=1`; отдельные реализации
проверяются через `generated` (см. `add_specializations` в списке).

Для каждого обычного сценария общий запуск:

- Выполняет исходник с `--optimize none --native off` и сохраняет эталонный вывод.
- Выполняет его с `--native required`, сравнивает stdout с эталоном и проверяет
  `preparation=ready`, точные `attempts/succeeded`, отсутствие `retries`.
- Компилирует нативную `.gbin`/DLL/SO пару, удаляет рабочий исходник, запрещает
  компилятор через `CC` и повторяет сравнение при запуске сохранённой программы.

Оба запуска должны завершиться успешно и без stderr. Сравнение сохраняет пробелы,
пустые строки и встроенные NUL; нормализуются только Windows CRLF. У VM отдельно
проверяются нулевые счётчики нативных вызовов. `expected_output.txt` дополнительно
защищает от одинаковой ошибки в обоих режимах.

Другие поля записи: `generated` задаёт минимальное количество вхождений фрагментов
в сгенерированном C; `output_contains` — обязательные фрагменты вывода;
`example` — пример из корня репозитория, который должен совпадать с тестом;
`driver` — C-драйвер для проверки экспортированного адаптера с настоящим контекстом памяти.

Записи `check` запускают специальные проверки: массовые сравнения арифметики,
доступа, libm и строковой библиотеки через внешний хост. Допустимы только встроенные
ID `runner`, `access`, `libm`, `runtime_strings`, `string_method_errors`, `saturation`.
Они вызывают C targets, стоящие рядом с policy selftest; Python не нужен.
Пути к скриптам и неизвестные ID отклоняются до выбора сценариев. Проверки используют
тот же список, но сохраняют свои проверки ошибок, ABI и границ. Низкоуровневые
проверки загрузчика/компилятора и sanitizer-конфигурации остаются отдельными шагами CI.

Каждый сценарий создаёт новый `BUILD/native-functional/scenario-*`; `case.name` указывает имя теста. Там остаются stdout,
stderr, команды, нативные отчёты и сгенерированные файлы. Ошибка одного теста не
мешает выполнить остальные; при любом падении общий код возврата ненулевой.
CI сохраняет весь этот каталог в артефакте `native-execution-*`.

## English

`list.json` is the single registry for native functional coverage. Add a directory
containing `program.goat`, a registry entry with a positive `calls` count, and an
optional exact UTF-8 `expected_output.txt`; no CI edits are needed. `--only` selects
registry names. On Windows use C runner executables, `goat.exe`, and MinGW GCC on PATH.

Each program runs first on the unoptimized VM and then with `--native required`.
The runner compares output bytes (normalizing only CRLF), rejects stderr/failures,
and verifies actual adapter entries with zero retries. It also compares saved
native execution without the source or compiler against the same VM result.
`bound` counts bound Goat functions, not their specializations: multiple signatures
of one function still give `bound=1`. The `add_specializations` case verifies each
implementation through `generated`. Optional `bound`, `generated`, `output_contains`, `example` and `driver` fields
preserve signature, generated-code, example consistency and adapter boundary checks.

Specialized `check` entries run the differential arithmetic/libm/runtime suites
from the same registry. Loader/compiler and sanitizer configurations remain separate
CI infrastructure checks. Only built-in check IDs `runner`, `access`, `libm`,
`runtime_strings`, `string_method_errors` and `saturation` are accepted. They invoke
C executables beside the policy selftest. Script paths and unknown IDs are rejected
before scenario selection; Python is not required. Every case retains diagnostics in a fresh
`BUILD/native-functional/scenario-*` directory (identified by `case.name`),
continues after individual failures, and exits nonzero if any test fails.

## Unified module execution (#316)

The `calls` and `bound` fields describe module ABI v1 execution; generated-C
checks and standalone drivers still exercise the internal C lowering directly.
ABI v1 accepts Unicode scalars, while Goat characters can contain any uint32.
For `bool_char`, five calls with non-scalar character arguments stay in the VM,
and `loop` has an unproven `chr` construction: 60 native calls, 20 bound functions.
For `string_conversions`, the two calls to `values(n)` stay in the VM because
`chr(n)` is not proven scalar: 14 native calls. Outputs remain unchanged and are
compared with the VM and frozen expected output. `string_methods` still requires
all 19 calls: its `chr(65)` argument is a proven scalar and must remain native.
