# Native support library / Библиотека поддержки нативного кода

## Русский

Генерируемый C-модуль подключает `native_runtime.h`. В самом модуле остаются
специализации пользовательских функций, их адаптеры и таблица метаданных.
Определения ABI, насыщающая арифметика, точное сравнение integer/real,
проверки платформы и защита глубины нативных вызовов вынесены в библиотеку.

Библиотека находится в `include/goat/native_runtime.h` и использует
`include/goat/native_abi.h`. Это библиотека в заголовках: короткие операции
могут встраиваться компилятором, а готовая DLL/SO не требует отдельной
библиотеки Goat во время загрузки. Состояние защиты стека локально для потока
и единицы трансляции; один генерируемый модуль собирается одной единицей
трансляции. Не следует разносить его функции и адаптеры по разным C-файлам.

`--save-c` сохраняет рядом с C-файлом оба необходимых заголовка. Существующие
одинаковые заголовки используются повторно; отличающиеся файлы не
перезаписываются, и экспорт завершается ошибкой. Для экспорта другой версии
библиотеки используй новую папку или явно убери старые заголовки.
`--print-c` только печатает исходник: при ручной компиляции добавь путь
`include/goat` через `-I` либо положи оба заголовка рядом с сохранённым C-файлом.

Пример ручной сборки Linux:

```sh
./goat --save-c program.goat
cc -std=c11 -O2 -fPIC -shared -fno-fast-math -ffp-contract=off program.c -lm -o program.so
```

Для MinGW используй `-shared -static -static-libgcc -DGOAT_NATIVE_BUILD`
и разрядность, совпадающую с интерпретатором. Автоматическая сборка
(`--native`, `--save-library`) сама подготавливает заголовки в приватной
временной папке и удаляет их после компиляции. Репозиторий и дополнительные
пути установки не нужны: заголовки встроены в исполняемый файл интерпретатора.

После изменения публичных заголовков выполни:

```sh
python3 scripts/embed_native_runtime.py
python3 scripts/embed_native_runtime.py --check
```

Встроенная копия генерируется из публичных заголовков, не редактируется
вручную и проверяется CI. Обычная сборка интерпретатора не требует Python.
ABI загрузчика остаётся версии 1.

## English

Generated C includes `native_runtime.h` and contains only program
specializations, their adapters and module metadata. The header-only support
library owns the ABI declarations, saturating integer arithmetic, exact mixed
numeric comparison, platform checks and native recursion/stack guards.
Inlining remains available and the resulting DLL/SO has no additional Goat
runtime dependency. Guard state is thread-local and translation-unit-local;
keep each generated module in one translation unit.

`--save-c` exports `native_runtime.h` and `native_abi.h` beside the C file.
Identical existing headers are reused; conflicting files are preserved and
cause export to fail. Use a fresh directory when exporting another runtime
version. `--print-c` writes only source: supply `-I/path/to/include/goat` when
compiling it manually, or place both public headers beside it.

Automatic native compilation stages the bundled headers in its private
workspace, then removes them. It does not depend on the checkout or an
installed SDK. After editing public headers, regenerate their embedded copy
with `python3 scripts/embed_native_runtime.py`; CI runs its `--check` mode.
Normal builds do not require Python. The loader ABI remains version 1.
