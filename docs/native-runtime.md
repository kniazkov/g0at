# Native support library / Библиотека поддержки нативного кода

## Русский

Генерируемый C-модуль подключает `native_runtime.h`. В самом модуле остаются
специализации пользовательских функций, адаптеры и таблица метаданных.
В публичных заголовках `include/goat` находятся ABI, объявления строковых
операций и короткие обёртки. Арифметические операции и защита стека остаются
встраиваемыми; состояние защиты стека локально для потока и единицы трансляции.
Не следует разносить функции одного генерируемого модуля по разным C-файлам.

Строковые алгоритмы реализованы в `src/native/strings.c` и отдельно собираются
в статическую библиотеку `libgoat_native.a` (цель CMake `goat_native`). Она не
содержит VM или собственного аллокатора: память и локаль обслуживаются через
таблицу сервисов интерпретатора. Библиотека собирается с PIC и без LTO, чтобы
подлинковываться к DLL/SO совместимым C-компилятором. Архив должен соответствовать
ОС, архитектуре, CRT и ABI интерпретатора. Нужный код включается в результирующую
DLL/SO; отдельная динамическая библиотека Goat при запуске не требуется.

Релизный пакет содержит `bin/goat`, `include/goat/*.h` и `lib/libgoat_native.a`.
При этом готовый архив вместе с заголовками встроен и в интерпретатор. Для
автоматической компиляции (`--native`, `--save-library`) интерпретатор извлекает
их в приватный временный каталог, передаёт архив линкеру **после C-файла** и
удаляет временные файлы. Поиск библиотек в исходниках, PATH или текущей папке
не нужен; перенос одного исполняемого файла сохраняет эту возможность.

`--save-c` сохраняет рядом с C-файлом три заголовка и `libgoat_native.a`.
Идентичные файлы переиспользуются; отличающиеся не перезаписываются, и экспорт
завершается ошибкой. Для новой версии используй новый каталог или явно убери
старые файлы SDK. Ручная сборка экспортированного модуля на Linux:

```sh
./goat --save-c program.goat
cc -std=c11 -O2 -fPIC -shared -fno-fast-math -ffp-contract=off program.c libgoat_native.a -lm -o program.so
```

`--print-c` печатает только C. При ручной сборке с установленным SDK:

```sh
cc -std=c11 -O2 -fPIC -shared -fno-fast-math -ffp-contract=off -I/path/to/package/include/goat program.c /path/to/package/lib/libgoat_native.a -lm -o program.so
```

Для MinGW используй `-shared -static -static-libgcc -DGOAT_NATIVE_BUILD`,
`-fno-fast-math -ffp-contract=off` и разрядность, совпадающую с интерпретатором.
Архив также ставится после исходника/объектных файлов, перед `-lm`.

CMake сначала собирает архив, затем `scripts/embed_native_runtime.cmake`
генерирует его встроенную копию и заголовки в
`<каталог сборки>/generated/native_runtime_data.inc`. Изменение реализации,
заголовков или генератора обновляет зависимые результаты. Ни архив, ни `.inc`
не хранятся в Git; Python для сборки не требуется.

ABI загрузчика — версии 2. Подробнее: [контекст памяти](native-memory.md),
[строковые операции](native-strings.md).

## English

Generated C includes `native_runtime.h` and contains program specializations,
adapters and metadata. Public headers contain the ABI, string declarations and
small wrappers. Numeric helpers and stack guards remain inline; guard state is
thread-local and translation-unit-local, so keep a generated module in one C file.

String algorithms live in `src/native/strings.c`, built separately as
`libgoat_native.a` (CMake target `goat_native`). The library contains no VM or
allocator: memory and locale services are dispatched to the interpreter. It is
built with PIC and without LTO for linking into native DLL/SO modules. Match the
archive's OS, architecture, CRT and ABI to the interpreter. Linked code becomes
part of the resulting module; no additional Goat DLL/SO is needed at execution.

Release packages contain `bin`, `include/goat` and `lib/libgoat_native.a`.
The interpreter also embeds the matching archive and headers: automatic native
compilation stages them privately, links the archive after the generated source,
and removes the temporary SDK. No checkout or installation-path discovery is
required, even when only the interpreter executable is relocated.

`--save-c` exports three headers and the archive beside the C source. Identical
files are reused; conflicting files are preserved and cause export failure.
Use a new directory when upgrading the SDK. The commands above show Linux
manual linking of exported C and `--print-c` output with an installed package.
On MinGW use `-shared -static -static-libgcc -DGOAT_NATIVE_BUILD` and strict
floating-point options, matching the interpreter bitness. Link the archive
after source/object files and before `-lm`.

CMake builds the archive before generating the embedded bytes in
`<build directory>/generated/native_runtime_data.inc`. Implementation/header
changes trigger dependent rebuilds. Binary archives and generated `.inc` files
are not tracked in Git; the build-time generator requires CMake, not Python.
The loader uses ABI v2 with a per-call [memory context](native-memory.md).
See also [native string operations](native-strings.md).
