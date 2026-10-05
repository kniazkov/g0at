# Язык программирования Goat

[![Сборка и тесты](https://github.com/kniazkov/g0at/actions/workflows/build_and_test.yml/badge.svg)](https://github.com/kniazkov/g0at/actions/workflows/build_and_test.yml)
[![Лицензия: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE.txt)

[English version](README.md)

**Goat — экспериментальный язык программирования, реализованный на C, с байткодовой виртуальной машиной, статическим анализом и нативным бэкендом.**

Проект Ивана Князькова служит для исследования устройства языков программирования и даёт работающие примеры для технических обсуждений и консультаций. Исходный код позволяет проследить путь программы от текста до исполнения.

## Документация

**[Goat изнутри — русский](docs/book/ru/index.md)** · **[Goat Internals — English](docs/book/en/index.md)**

Книга описывает реализованную систему на двух языках с одинаковыми структурой и содержанием. Она охватывает лексический и синтаксический анализ, байткод, исполнительную среду, статический анализ, генерацию нативного кода, тестирование и измерение производительности. Объяснения сопровождаются примерами; ссылки на исходный код указывают ревизию описываемой реализации.

| Тема | Русский | English |
| --- | --- | --- |
| Язык | [Справочник](docs/book/ru/appendix-b-language.md) | [Reference](docs/book/en/appendix-b-language.md) |
| Встроенные функции | [Справочник](docs/book/ru/appendix-d-builtins.md) | [Reference](docs/book/en/appendix-d-builtins.md) |
| Параметры командной строки | [Интерфейсы запуска](docs/book/ru/appendix-e-launch-interfaces.md) | [Launch interfaces](docs/book/en/appendix-e-launch-interfaces.md) |
| Устройство исходного кода | [Карта реализации](docs/book/ru/appendix-f-implementation-map.md) | [Implementation map](docs/book/en/appendix-f-implementation-map.md) |
| Текущие ограничения | [Границы реализации](docs/book/ru/appendix-g-implementation-boundaries.md) | [Implementation boundaries](docs/book/en/appendix-g-implementation-boundaries.md) |

## Пример на Goat

Функции являются значениями. Внутренняя функция может сохранять доступ к окружающей области видимости (замыкание):

```text
const add = func(a) {
    return func(b) {
        return a + b;
    };
};
const add_two = add(2);
println(add_two(3));
```

Результат: `5` с переводом строки. Другие программы находятся в [примерах книги](docs/book/examples) и [функциональных тестах](test/functional).

Цикл в форме языка C использует те же значения и области видимости:

```text
var sum = 0;
for (var i = 0; i < 5; i++) sum = sum + i;
println(sum);
```

Он выводит `10`. Части заголовка могут быть пустыми; тело может быть одиночным оператором или блоком. [Глава 2](docs/book/ru/02-implemented-language.md) описывает области видимости и управление исполнением, а [глава 22](docs/book/ru/22-c-code-generation.md) показывает нативный числовой цикл.

Goat поддерживает динамически типизированные значения, лексические области видимости, функции и рекурсию, условные ветвления, циклы `for` в форме языка C, объекты на основе блоков, исключения, математические функции и ввод-вывод. Статический анализ отслеживает приближённые значения, вызовы функций, эффекты и числовые специализации. Подходящие функции можно перевести на C и исполнять как машинный код через разделяемую библиотеку (нативное исполнение).

> [!CAUTION]
> Goat — экспериментальная реализация. Синтаксиса `while`, `break`, `continue`, индексирования массивов, обращения к свойствам и импорта модулей пока нет. Нативная генерация охватывает ограниченное числовое подмножество; остальные поддерживаемые конструкции языка исполняет VM. В книге ограничения реализации отмечены красными блоками.

## Сборка и запуск

Получите репозиторий и перейдите в его каталог:

```sh
git clone https://github.com/kniazkov/g0at.git
cd g0at
```

### Linux

Установите GCC или Clang, GNU Make, Bash, CMake версии 3.13 или новее и стандартные библиотеки для разработки на C, включая pthread и libm.

```bash
bash scripts/build.sh
./goat example/hello_world.goat
```

Скрипт сборки Debug копирует `goat` в корень репозитория и запускает модульные тесты, тесты анализа и функциональные тесты исполнения. Пример выводит `it works!`.

Для Clang с отдельным каталогом сборки:

```bash
CC=clang BUILD_DIR=build/clang bash scripts/build.sh
```

Для сборки Release без запуска тестов:

```bash
bash scripts/build_release.sh
```

Исполняемый файл Release помещается в `build/release` и копируется в корень репозитория. Этот скрипт также учитывает `CC` и `BUILD_DIR`. В Release отключены собственный учёт выделений памяти и защитные проверки (`MEMORY_DEBUG`); в Debug они сохранены.

### Windows (MinGW)

Установите CMake версии 3.13 или новее и комплект MinGW с GCC, `mingw32-make` и поддержкой pthread, доступный через `PATH`. В PowerShell из корня репозитория:

```powershell
.\scripts\build_mingw.cmd
.\goat.exe .\example\hello_world.goat
```

Скрипт Debug собирает интерпретатор и запускает те же три набора тестов. Для сборки Release без тестов:

```powershell
.\scripts\build_release_mingw.cmd
```

Он создаёт `build\release\goat.exe` и копирует его в корень репозитория.

### Нативное исполнение и сохранённые программы

Обычный запуск исходного текста использует байткод и не требует компилятора C во время исполнения. Чтобы проверить нативное исполнение и посмотреть отчёт:

```bash
./goat --native required --print-native docs/book/examples/22-native-module.goat
```

В PowerShell замените `./goat` на `.\goat.exe`. Для нативной подготовки нужен совместимый компилятор C: `CC` задаёт одно имя или путь к исполняемому файлу; по умолчанию используются `cc` в Linux и `gcc` в Windows. Новые библиотеки MinGW статически связываются с библиотеками поддержки компилятора, поэтому пользователю не требуется копировать `libgcc_s_*.dll` или `libwinpthread-1.dll` рядом с Goat; комплект компилятора должен содержать статические архивы.

`--native required` требует хотя бы одну привязанную нативную функцию. Он не требует нативного исполнения каждого вызова. `--native auto` допускает исполнение байткода при неудачной подготовке. Оба режима требуют оптимизации, которая включена по умолчанию.

Чтобы сохранить байткод без исполнения программы, а затем запустить его:

```bash
mkdir -p build/quickstart
cp docs/book/examples/22-native-module.goat build/quickstart/program.goat
./goat --compile build/quickstart/program.goat
./goat --run build/quickstart/program.gbin
```

Эквивалент для PowerShell:

```powershell
New-Item -ItemType Directory -Force .\build\quickstart | Out-Null
Copy-Item .\docs\book\examples\22-native-module.goat .\build\quickstart\program.goat
.\goat.exe --compile .\build\quickstart\program.goat
.\goat.exe --run .\build\quickstart\program.gbin
```

Добавление `--native required` в команду компиляции также создаёт сопутствующую библиотеку `.so` или `.dll`. Храните её рядом с `.gbin` с одинаковым базовым именем. Для исполнения сохранённой пары не нужны ни исходный текст, ни компилятор C. Правила совместимости и обработки ошибок описаны в главе [«Раздельная компиляция»](docs/book/ru/26-separate-compilation.md).

## Наблюдение и тестирование

`--print-source-code`, `--print-bytecode` и `--print-analysis` показывают промежуточные представления; эти параметры также запускают программу. `--save-graph graph.svg` сохраняет изображение AST и требует Graphviz (`dot` в `PATH`). `--print-c` экспортирует сгенерированный C без исполнения программы. Полные примеры приведены в главе [«Средства наблюдения»](docs/book/ru/27-observability.md).

Скрипты сборки запускают три основных набора тестов. Дополнительные скрипты проверяют сгенерированный C, нативный ABI, загрузку, диспетчеризацию, сохранённые программы и производительность. CI использует Linux с GCC/Clang и Windows с MINGW32/MINGW64/UCRT64. Глава [«Тестирование»](docs/book/ru/28-testing.md) описывает наборы, форматы тестовых данных и проверки CI. [Инструкция к бенчмарку](test/performance/README.md) объясняет запуск проверки производительности в Release.

## Автор и лицензия

Автор — [Иван Князьков](https://github.com/kniazkov).

Goat распространяется по [лицензии MIT](LICENSE.txt).
