# Нативные функциональные тесты / Native functional tests

```sh
python3 scripts/check_native.py build/goat "build/native functional"
python3 scripts/check_native.py build/goat "build/native functional" --only quadratic string_chain
```

На Windows используй обычный Windows Python и `build/goat.exe`, с MinGW GCC в PATH.
Запускать можно из любого каталога, если указать путь к скрипту и интерпретатору.

`list.json` — общий список. Новый обычный тест добавляется без правки CI:

1. Создай `test/native/<name>/program.goat`.
2. Добавь запись `{"name": "<name>", "calls": 1}` в `list.json`.
3. При необходимости добавь `expected_output.txt` с точным UTF-8-выводом.

`calls` — число входов из VM в нативный адаптер; вложенные вызовы внутри нативного
кода не увеличивают этот счётчик. Оно обязательно положительное. Опциональное
`bound` задаёт точное число привязанных адаптеров.

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

Записи `check` запускают специальные проверки из `checks/`: массовые сравнения
арифметики и libm, проверку строковой библиотеки через внешний хост. Они используют
тот же список, но сохраняют свои проверки ошибок, ABI и границ. Низкоуровневые
проверки загрузчика/компилятора и sanitizer-конфигурации остаются отдельными шагами CI.

Каждый запуск создаёт новый `run-*` в выходном каталоге. Там остаются stdout,
stderr, команды, нативные отчёты и сгенерированные файлы. Ошибка одного теста не
мешает выполнить остальные; при любом падении общий код возврата ненулевой.
CI сохраняет весь этот каталог в артефакте `native-execution-*`.

## English

`list.json` is the single registry for native functional coverage. Add a directory
containing `program.goat`, a registry entry with a positive `calls` count, and an
optional exact UTF-8 `expected_output.txt`; no CI edits are needed. `--only` selects
registry names. On Windows use native Python, `goat.exe`, and MinGW GCC on PATH.

Each program runs first on the unoptimized VM and then with `--native required`.
The runner compares output bytes (normalizing only CRLF), rejects stderr/failures,
and verifies actual adapter entries with zero retries. It also compares saved
native execution without the source or compiler against the same VM result.
Optional `bound`, `generated`, `output_contains`, `example` and `driver` fields
preserve signature, generated-code, example consistency and adapter boundary checks.

Specialized `check` entries run the differential arithmetic/libm/runtime suites
from the same registry. Loader/compiler and sanitizer configurations remain separate
CI infrastructure checks. Every run retains diagnostics in a fresh `run-*` directory,
continues after individual failures, and exits nonzero if any test fails.
