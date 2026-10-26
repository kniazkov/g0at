# Goat portable package / Переносимый пакет Goat

## Русский

Распакуй архив в любой каталог. Исходники Goat, CMake и Python для использования
пакета не нужны. `bin/goat` (Windows: `bin/goat.exe`) — релизный интерпретатор;
`include/goat` — публичные заголовки ABI и библиотеки поддержки нативного кода;
`lib/libgoat_native.a` — статическая библиотека строковых операций.
В `examples` лежат примеры, в `docs` — описание ABI, в `build-info.txt` — ревизия
и сведения о сборке. Можно добавить `bin` в PATH.

Команды ниже выполняются из корня распакованного пакета (Windows: замени
`bin/goat` на `bin\goat.exe`):

```sh
bin/goat --native off examples/hello_world.goat
bin/goat --compile --optimize none examples/numeric.goat
bin/goat --run --native off examples/numeric.gbin
bin/goat --compile --native required examples/numeric.goat
bin/goat --run --native required examples/numeric.gbin
bin/goat --save-c examples/numeric.goat
```

Только создание нативной DLL/SO требует установленного C11-компилятора GCC
или Clang (на Windows — MinGW GCC) той же архитектуры, что и интерпретатор.
По умолчанию используется GCC; переменная `CC` задаёт имя или путь к одному
исполняемому файлу компилятора, без дополнительных аргументов.
Заголовки и статическая библиотека для автоматической сборки уже встроены
в интерпретатор.
Для ручной сборки вывода `--print-c` добавь `-I<пакет>/include/goat` и `<пакет>/lib/libgoat_native.a` после C-файла;
полные команды описаны в [native-runtime.md](docs/native-runtime.md).

Готовый модульный `.gbin` содержит байткод и собственные нативные варианты;
исходник и C-компилятор для запуска не нужны. Старый формат отдельной программы
может использовать лежащую рядом одноимённую `.so`/`.dll`. Нативный вариант должен
соответствовать ОС, архитектуре, runtime и поддерживаемому ABI интерпретатора.
Режим `--native off` не требует C-компилятора.

Linux-пакет зависит от того семейства системной libc, с которым он собран:
glibc (и libm) либо musl. Исполняемые файлы и статическая библиотека пакетов разных
семейств не взаимозаменяемы; выбирай соответствующую среду не старее среды сборки.
Это ограничение исполняемых файлов пакета не меняет переносимую основную часть
модульного `.gbin`: байткод и mainHash одинаковы для glibc/musl, а выбор native
проверяет runtime текущего процесса и версии каждого варианта. Windows-пакет
включает MinGW runtime статически и не требует MSYS2 на целевом компьютере.

## English

Extract anywhere; no Goat checkout, CMake or Python is needed. `bin` contains
the Release interpreter, `include/goat` the public headers, `lib/libgoat_native.a`
the static support library, `examples`
sample programs, `docs` ABI documentation, and `build-info.txt` build identity.
The commands above work from the package root; use `bin\goat.exe` on Windows.

VM execution needs no C compiler. Creating native modules requires C11 GCC
or Clang (MinGW GCC on Windows) matching the interpreter architecture. `CC`
selects one compiler executable, without flags; the default is GCC. Automatic
compilation uses embedded headers and the matching static archive. For manual compilation of `--print-c`
output use `-I<package>/include/goat` and link `<package>/lib/libgoat_native.a` after the C file; see [native-runtime.md](docs/native-runtime.md).

A ready module-format `.gbin` contains its bytecode and native variants and runs
without sources or a C compiler. The legacy standalone format may use an adjacent
same-named `.so`/`.dll`. A native variant must match the interpreter's OS,
architecture, runtime and supported ABI.

A Linux package depends on the system libc family it was built against: glibc
(and libm) or musl. Executables and static support archives from different runtime
families are not interchangeable; choose the matching environment at least as
recent as the build environment. This executable-package constraint does not
change a module `.gbin`'s portable main part: bytecode and mainHash are identical
across glibc/musl, while native selection checks the running process's runtime
and each variant's versions. Windows statically includes the MinGW runtime and
does not require MSYS2 on the destination computer.
