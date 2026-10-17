# Goat portable package / Переносимый пакет Goat

## Русский

Распакуй архив в любой каталог. Исходники Goat, CMake и Python для использования
пакета не нужны. `bin/goat` (Windows: `bin/goat.exe`) — релизный интерпретатор;
`include/goat` — публичные заголовки ABI и библиотеки поддержки нативного кода.
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
Заголовки для автоматической сборки уже встроены в интерпретатор.
Для ручной сборки вывода `--print-c` добавь `-I<пакет>/include/goat`;
полные команды описаны в [native-runtime.md](docs/native-runtime.md).

Для запуска готовой программы достаточно `.gbin` и лежащей рядом одноимённой
`.so`/`.dll`; исходник программы и C-компилятор уже не нужны. Нативные модули
должны соответствовать ОС, архитектуре и ABI интерпретатора. Пересобирай их
при обновлении интерпретатора. Режим `--native off` не требует C-компилятора.
Linux-пакет использует системные glibc/libm: выбирай совместимую систему
(как минимум не старее среды сборки). Windows-пакет включает MinGW runtime
статически и не требует установки MSYS2 на целевом компьютере.

## English

Extract anywhere; no Goat checkout, CMake or Python is needed. `bin` contains
the Release interpreter, `include/goat` the public native SDK, `examples`
sample programs, `docs` ABI documentation, and `build-info.txt` build identity.
The commands above work from the package root; use `bin\goat.exe` on Windows.

VM execution needs no C compiler. Creating native modules requires C11 GCC
or Clang (MinGW GCC on Windows) matching the interpreter architecture. `CC`
selects one compiler executable, without flags; the default is GCC. Automatic
compilation uses embedded headers. For manual compilation of `--print-c`
output use `-I<package>/include/goat`; see [native-runtime.md](docs/native-runtime.md).

Deploy a compiled `.gbin` together with its same-named `.so`/`.dll` to run
without program source or compiler. Native modules must match the interpreter
OS, architecture and ABI; rebuild them when upgrading the interpreter.
Linux requires compatible system glibc/libm (use a system at least as recent
as the build environment). Windows statically includes the MinGW runtime and
does not require MSYS2 on the destination computer.
