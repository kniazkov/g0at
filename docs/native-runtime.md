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

`--save-c` сохраняет рядом с C-файлом все необходимые заголовки. Существующие
одинаковые заголовки используются повторно; отличающиеся файлы не
перезаписываются, и экспорт завершается ошибкой. Для экспорта другой версии
библиотеки используй новую папку или явно убери старые заголовки.
`--print-c` только печатает исходник: при ручной компиляции добавь путь
`include/goat` через `-I` либо положи все заголовки рядом с сохранённым C-файлом.

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

CMake автоматически генерирует встроенную копию заголовков в
`<каталог сборки>/generated/native_runtime_data.inc` перед компиляцией интерпретатора.
При изменении любого публичного заголовка или скрипта генерации зависимые файлы пересобираются.
Генератор `scripts/embed_native_runtime.cmake` использует только CMake: Python не нужен.
Сгенерированный файл не хранится в Git и не записывается в дерево исходников;
разные каталоги сборки имеют независимые копии.

ABI загрузчика — версии 2; [контекст памяти](native-memory.md) передаётся каждому адаптеру.

## English

Generated C includes `native_runtime.h` and contains only program
specializations, their adapters and module metadata. The header-only support
library owns the ABI declarations, saturating integer arithmetic, exact mixed
numeric comparison, platform checks and native recursion/stack guards.
Inlining remains available and the resulting DLL/SO has no additional Goat
runtime dependency. Guard state is thread-local and translation-unit-local;
keep each generated module in one translation unit.

`--save-c` exports `native_runtime.h`, `native_abi.h` and `native_strings.h` beside the C file.
Identical existing headers are reused; conflicting files are preserved and
cause export to fail. Use a fresh directory when exporting another runtime
version. `--print-c` writes only source: supply `-I/path/to/include/goat` when
compiling it manually, or place all public headers beside it.

Automatic native compilation stages the bundled headers in its private
workspace, then removes them. It does not depend on the checkout or an
installed SDK. CMake generates the embedded copy automatically into
`<build directory>/generated/native_runtime_data.inc` before compiling the interpreter.
Changes to any public header or the generator trigger regeneration and recompilation.
The generator `scripts/embed_native_runtime.cmake` needs only CMake, not Python.
The generated file is not tracked in Git and never written into the source tree;
separate build directories have independent copies.
The loader uses ABI v2 with a per-call [memory context](native-memory.md).

Native string operations / Нативные строковые операции: [native-strings.md](native-strings.md).
