# Нативные булевы значения и символы / Native booleans and characters

ABI v2 поддерживает отдельные теги `GOAT_NATIVE_BOOL = 4` и `GOAT_NATIVE_CHAR = 5`.
Оба типа могут быть аргументами и результатами функций, локальными переменными,
передаваться во вложенные и рекурсивные вызовы. Они не смешиваются с числами при
выборе специализации: `true`, `1` и `chr(1)` имеют разные сигнатуры.

| Тип Goat | Поле `goat_native_value_v2_t.value` | Тип внутри генерируемой функции |
|---|---|---|
| Булево | `uint32_t boolean`, строго 0 или 1 | `bool` |
| Символ | `uint32_t character`, полный диапазон uint32_t | `uint32_t` |

Размер и выравнивание ABI-значения не меняются. Версия остаётся 2; старые числовые
и строковые модули совместимы. Старый загрузчик, не знающий новые теги, отвергнет
модуль с ними. Передача C `_Bool` через границу ABI не требуется. Неканоническое
булево значение отклоняется адаптером на входе и интерпретатором на выходе с
`GOAT_NATIVE_BAD_REQUEST`; прежний результат адаптера остаётся нетронутым.

Символ соответствует существующей семантике `chr`/`ord`: допускаются нулевое
значение, суррогаты и значения вне диапазона Unicode. ABI не нормализует их и
не сужает до `wchar_t`. Преобразование символа в строку по-прежнему следует правилам
VM и кодировке хоста; строковый `charAt` возвращает кодовую единицу `wchar_t`.

```goat
const empty = func(s) { return s.isEmpty(); };
const first = func(s) { return s.charAt(0); };
const invert = func(value) { return !value; };
println(empty(""));
println(first("Goat"));
println(invert(true));
```

Поддерживаются `!`, `!!`, `&&`, `||`, сравнения булевых значений и символов,
условия и циклы. Правая часть `&&`/`||` вычисляется только при необходимости,
включая её вызовы и побочные эффекты. `int` преобразует булево значение в 0/1;
`str`, `format`, `chr` и `ord` работают с соответствующими нативными типами.
Сложение с символом слева создаёт строку, как в VM. Сравнение на равенство с
числом не превращает символ или булево значение в число. Упорядочивание между
несовместимыми типами не получает ошибочную числовую реализацию.

Общий тест `bool_char` сравнивает VM, обязательное нативное исполнение и запуск
сохранённого модуля без исходника/компилятора. C-драйвер отдельно проверяет теги,
канонические булевы значения, границы символов и неизменность ABI-layout.

## English

ABI v2 adds `GOAT_NATIVE_BOOL = 4` and `GOAT_NATIVE_CHAR = 5`, without changing
value size or alignment. The union fields are `uint32_t boolean` (only 0 or 1)
and `uint32_t character` (the entire uint32_t domain). Internal generated functions
use `bool` and `uint32_t`. Older numeric/string modules remain compatible; older
loaders reject unknown tags. Adapters reject noncanonical boolean arguments, and
the host rejects noncanonical results before committing VM state.

Both types support arguments, returns, locals, nested calls and recursion. Exact
specialization dispatch distinguishes booleans, characters, integers and reals.
Logical operators preserve short-circuit evaluation and operand effects. Native
comparisons, conditions, loops, boolean-to-integer conversion, character-to-string
concatenation and string method results match VM semantics. Characters are not
Unicode-validated or narrowed at the ABI boundary; string conversion remains
host-dependent, just as in the VM. The `bool_char` suite and its external C driver
cover direct/saved native execution, invalid encodings and layout compatibility.
