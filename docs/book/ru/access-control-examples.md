# Модификаторы доступа: примеры и угловые случаи

> [Контракт модулей](modules.md) задаёт реализованные границы доступа: без модификатора — свой исходный файл, `public` — свой модуль, `external` — другие модули. `private`/`protected` сохраняют правила объектных доменов. Экспорт объекта не экспортирует его поля рекурсивно.

[Оглавление](index.md) · [English](../en/access-control-examples.md) · [Правила](appendix-i-access-control.md) · [План](access-control-implementation-plan.md)

Эти примеры описывают модель доступа, реализованную анализатором и VM. Доказанные запреты диагностируются до исполнения, неизвестные случаи сохраняют динамические проверки. Каждый пример независим; ожидаемые результаты описаны словами, вызовы `print` не подразумевают автоматические переводы строк. Отрицательные случаи проверяются отдельно, в том числе статически и при динамически неизвестном получателе. Все примеры относятся к стандартным встроенным `clone` и `flat`, а не пользовательским переопределениям.

<a id="ac-01"></a>

## AC-01. Объявления и сокращённая форма

```goat
public f = func() { return 1; };
public const g = func() { return 2; };
private var x = 3, y = 4;
protected left = 5, right = 6;
```

f и g — публичные константы-функции; x и y — приватные переменные; left и right — защищённые константы. Применимость модификатора не зависит от области видимости.

<a id="ac-02"></a>

## AC-02. Недопустимые формы

```goat
public private var x = 1;
private obj.x = 2;
protected obj["x"] = 3;
```

Каждая строка — отдельный отрицательный пример. Нельзя указывать два модификатора или ставить модификатор перед присваиванием свойству. Для существующего свойства присваивание не меняет его защиту.

<a id="ac-03"></a>

## AC-03. Тело объекта и оба способа доступа

```goat
var obj = {
    private var x = 42;
    print(x);
    public read = func() { return this["x"]; };
};
print(obj.read());
print(obj["x"]);
```

Первые два print дают 42. Последний доступ запрещён, как и obj.x. В отрицательном тесте последнюю строку нужно проверять отдельно: статическая диагностика может предотвратить исполнение всей программы.

<a id="ac-04"></a>

## AC-04. Внешняя функция не становится привилегированной

```goat
const outside = func() { return this.x; };
var obj = {
    private var x = 10;
    public read = outside;
};
var copy = obj.clone();
print(copy.read());
```

Ошибка доступа. Ни присваивание outside свойству, ни clone не выдаёт права на private x. Тот же результат ожидается после flat().

<a id="ac-05"></a>

## AC-05. Перенос метода обычным присваиванием

```goat
var a = {
    private var x = 10;
    public read = func(other) { return other.x; };
};
var b = {
    private var x = 20;
};
print(a.read(a));
b.read = a.read;
print(b.read(b));
```

Первый вызов возвращает 10; последний не имеет доступа к приватному x объекта b. Перенос ссылки на метод не адаптирует его права. Это отличается от специальной адаптации методов при clone/flat. Явный аргумент важен: `this` обозначает лексическую область/область вызова, а не получателя Java/JS, меняющегося при сохранении функции в b.

<a id="ac-06"></a>

## AC-06. Независимые объекты одного шаблона

```goat
const make = func(value) {
    return {
        private var x = value;
        public sumWith = func(other) { return x + other.x; };
    };
};
var a = make(10);
var b = make(20);
print(a.sumWith(b));
```

Ошибка доступа к b.x. Совпадение AST или тела фабрики не заменяет идентичность владельца приватной области. Аналогичного доступа к независимому объекту не даёт и protected.

<a id="ac-07"></a>

## AC-07. Замыкание переживает вызов метода

```goat
var obj = {
    private var x = 42;
    public reader = func() {
        return func() { return x; };
    };
};
var read = obj.reader();
print(read());
```

42. Возврат функции наружу не лишает её уже существующего доступа. Внешний код получает возможность вызвать выданную функцию, но не прямой доступ к obj.x.

<a id="ac-08"></a>

## AC-08. Метод вложенного объекта

```goat
var outer = {
    private var secret = 42;
    public inner = {
        public read = func() { return secret; };
    };
};
print(outer.inner.read());
```

42: захват разрешён в месте объявления. Это не означает, что при поверхностном копировании outer вложенный inner будет скопирован (см. пример AC-16).

<a id="ac-09"></a>

## AC-09. Конструктор и методы потомка

```goat
const Base = {
    protected var x = 10;
    private var secret = 7;
    public readSecret = func() { return secret; };
};
var child = Base -> {
    public fromBody = x;
    public readBase = func() { return Base.x; };
};
print(child.fromBody);
print(child.readBase());
print(child.readSecret());
```

10, 10, 7. Конструктор и метод потомка имеют protected-доступ; унаследованный readSecret сохраняет приватное право Base. Новый метод потомка с прямым чтением secret должен быть отклонён. Правило protected действует и для потомка в другом модуле.

<a id="ac-10"></a>

## AC-10. Недоступное поле останавливает поиск

```goat
const Base = { public var x = 1; };
var obj = Base -> { private var x = 2; };
print(obj.x);
```

Ошибка, а не 1. Поиск не обходит запрещённое поле ради доступного поля прототипа. То же правило действует после flat().

<a id="ac-11"></a>

## AC-11. flat сохраняет скрытые данные

```goat
var obj = {
    private var x = 42;
    public getX = func() { return this.x; };
};
var copy = obj.flat();
print(copy.getX());
```

42. Поле переносится независимо от прав вызывающего кода, но copy.x и copy["x"] снаружи остаются запрещены. При печати и внешнем переборе ключ x не раскрывается.

<a id="ac-12"></a>

## AC-12. Два приватных x в цепочке из трёх объектов

```goat
const A = {
    private var x = 10;
    public getA = func() { return this.x; };
};
const B = A -> {
    private var x = 20;
    public getB = func() { return this.x; };
};
var obj = B -> {
    public sum = func() { return this.getA() + this.getB(); };
};
var copy = obj.flat();
print(copy.getA());
print(copy.getB());
print(copy.sum());
```

10, 20, 30. В копии два независимых приватных поля, несмотря на одинаковый ключ. Слияние по строке x недопустимо. Происхождение поля нужно для прав и связывания, но A и B не становятся прототипами копии.

<a id="ac-13"></a>

## AC-13. Перекрытый protected и захват по имени

```goat
const A = {
    protected var x = 10;
    public getCaptured = func() { return x; };
    public getCurrent = func() { return this.x; };
};
var obj = A -> { protected var x = 20; };
var copy = obj.flat();
print(copy.getCaptured());
print(copy.getCurrent());
```

10 и 20, как до flat(). Захват хранит связь с конкретной ячейкой A, тогда как this.x выполняет поиск с учётом потомка. Внутренняя ячейка 10 сохраняется, хотя обычный поиск выбирает 20.

<a id="ac-14"></a>

## AC-14. Несколько методов разделяют новое захваченное поле

```goat
var obj = {
    private var x = 10;
    public getX = func() { return x; };
    public setX = func(value) { x = value; };
    public viaThis = func() { return this.x; };
};
var copy = obj.clone();
copy.setX(20);
print(obj.getX());
print(copy.getX());
print(copy.viaThis());
```

10, 20, 20. Все три метода копии связаны с одной новой ячейкой x. Исходные функции и исходная ячейка не меняются. Для flat() ожидается то же.

<a id="ac-15"></a>

## AC-15. Внешний захват остаётся общим

```goat
var counter = 0;
var obj = {
    private var x = 10;
    public next = func() {
        counter += 1;
        x += 1;
        return x;
    };
};
var copy = obj.clone();
print(copy.next());
print(obj.next());
print(counter);
```

11, 11, 2. Каждая версия объекта имеет свою ячейку x, но обе функции изменяют один внешний counter. clone и flat не копируют окружающую область видимости.

<a id="ac-16"></a>

## AC-16. Граница поверхностного копирования

```goat
var obj = {
    private var x = 10;
    public setX = func(value) { x = value; };
    public inner = {
        public getX = func() { return x; };
    };
};
var copy = obj.clone();
copy.setX(20);
print(copy.inner == obj.inner);
print(copy.inner.getX());
obj.setX(30);
print(copy.inner.getX());
```

true, 10, 30. inner — общий объект; его функция не переносится непосредственно и продолжает читать x оригинала. Перенастроить общий inner только для copy нельзя. flat() также не обходит вложенные объекты рекурсивно.

<a id="ac-17"></a>

## AC-17. Явная ссылка на оригинал и сохранённое право

```goat
const A = {
    private var x = 10;
    public setLocal = func(value) { x = value; };
    public local = func() { return x; };
    public original = func() { return A.x; };
};
var copy = A.clone();
copy.setLocal(20);
print(copy.local());
print(copy.original());
```

20 и 10. Внешний захват A не подменяется копией; исходное право метода читать A.x сохраняется. Это не выдаёт исходному методу права читать приватные поля copy. С protected и с flat() действует то же различие.

<a id="ac-18"></a>

## AC-18. Оригинал не получает прав на клон

```goat
var obj = {
    private var x = 10;
    public readOther = func(other) { return other.x; };
};
var copy = obj.clone();
print(obj.readOther(copy));
```

Ошибка доступа. Разрешение скопированному методу сохранять прежнюю явную ссылку на оригинал не симметрично и не открывает оригиналу новый объект.

<a id="ac-19"></a>

## AC-19. Общий прототип при clone

```goat
const Base = {
    private var x = 0;
    public next = func() { x += 1; return x; };
};
var obj = Base -> {};
var copy = obj.clone();
print(obj.next());
print(copy.next());
```

1 и 2. В данном примере clone не переносит ни x, ни next: они принадлежат общему Base. Если заменить clone на flat, результаты будут 1 и 1: flat переносит поле и функцию в новую область.

<a id="ac-20"></a>

## AC-20. Два имени одной функции

```goat
var obj = {
    private var x = 10;
    public f = func() { return x; };
    public g = f;
};
var copy = obj.clone();
print(copy.f == copy.g);
print(copy.f == obj.f);
print(copy.g());
```

true, false, 10. Одна исходная функция требует одной адаптированной функции в этой операции. Её окружение отличается от исходного. Для flat() действует то же правило; независимые функции с одинаковым телом не объединяются.

<a id="ac-21"></a>

## AC-21. Ромб: одна ячейка общего предка

```goat
const A = {
    private var x = 0;
    public inc = func() { x += 1; return x; };
    public read = func() { return x; };
};
const Left = A -> {
    public viaLeft = func() { return this.inc(); };
};
const Right = A -> {
    public viaRight = func() { return this.read(); };
};
var obj = [Left, Right] -> {};
var copy = obj.flat();
print(copy.viaLeft());
print(copy.viaRight());
print(A.read());
```

1, 1, 0. A обрабатывается один раз, обе ветви используют одну ячейку копии. Исходная ячейка не меняется. Это проверяет дедупликацию по идентичности объекта.

<a id="ac-22"></a>

## AC-22. Приоритет поиска и единственный родитель

```goat
const Left = { public var x = 1; };
const Right = { public var x = 2; };
var obj = [Left, Right] -> {};
var copy = obj.flat();
print(copy.x == obj.x);
print(copy.instanceOf(Object));
print(copy.instanceOf(Left));
print(copy.instanceOf(Right));
```

true, true, false, false. Тест намеренно сравнивает с существующим поиском: произвольный порядок топологической сортировки не должен выбирать другого победителя. instanceOf наследуется от Object, а не копируется из него.

<a id="ac-23"></a>

## AC-23. Фильтрация наблюдения после полного копирования

Наблюдатель определяется исполняемым кодом, а не тем, кто вызвал его функцию:

```goat
const outside = func(value) { return str(value); };
const box = {
    private secret = 42;
    public inspect = func(value) {
        println(str(value));          // secret виден своему методу
        println(outside(value));      // внешняя функция secret не видит
        println([value].toString());  // вложенный объект: secret снова виден
    };
};
box.inspect(box);
println(box);                         // secret снова скрыт
const copy = box.flat();
copy.inspect(copy);                   // перенесённый метод имеет права на копию
box.inspect(copy);                    // исходный метод прав на копию не получил
```

Объект передан явно: `this` обозначает текущую область объекта, включая область вызова функции; это не Java/JS-получатель. `for (key in value)` использует те же права, но делает снимок только собственных доступных ключей. Отдельная внешняя функция перебора использует свои права. Права не сохраняются у вызывающего кода после возврата или исключения. Фильтр рекурсивен и для объектных ключей; циклы сокращаются до `{...}`/`[...]`. Если все поля скрыты, результат — `{ }`. Скрытые одноимённые ячейки внутреннего инвентаря flat не перечисляются и не печатаются даже при наличии прав на них; методы продолжают обращаться к своим ячейкам.

```goat
var obj = {
    private var secret = 42;
    public var shown = 7;
    public read = func() { return secret; };
};
var copy = obj.flat();
for (key in copy) print(key);
print(str(copy));
print(copy.read());
```

Внешний перебор не выдаёт secret, а стандартное строковое представление не раскрывает его поле или значение 42. read() возвращает 42. Точный текст функций и порядок печати здесь не задаются. Собственный метод с приватным доступом может видеть secret при наблюдении.

<a id="ac-24"></a>

## AC-24. Массив остаётся изменяемым

```goat
var obj = {
    private var items = [1];
    public getItems = func() { return items; };
    public getCopy = func() { return items.clone(); };
};
var shared = obj.getItems();
shared.push(2);
var separate = obj.getCopy();
separate.push(3);
print(obj.getItems());
```

Исходный массив содержит [1, 2]. Приватность поля не запрещает менять объект, возвращённый разрешённым методом, даже из другого модуля. Копия массива защищает только от изменения его собственных элементов; это не глубокое копирование вложенных значений.

<a id="ac-25"></a>

## AC-25. Фабрика и принадлежность объекта модулю

Модуль A (`main.goat` модуля `a@1`):

```goat
external make = func() {
    return {
        external var x = 1;
        var internal = 7;
    };
};
```

Модуль B (`main.goat`):

```goat
const A = import "a@1";
var obj = A.make();
obj.x = 2;
var copy = obj.clone();
copy.extra = 3;
var child = obj -> { var own = 4; };
```

Сначала соберите A и разместите его архив в `modules/a/1.gbin` рядом с `.gmod` модуля B (либо в репозитории, заданном через `GOAT_MODULE_PATH`). `obj` принадлежит A, поэтому отдельная попытка `obj.extra = 3` запрещена. `copy` и `child` принадлежат B. Чтение `obj.internal` или `copy.internal` из B запрещено: поле без модификатора сохраняет свой файл объявления в A. Замена объявления на `public var internal = 7` разрешила бы доступ другим файлам A, но не B.

<a id="ac-26"></a>

## AC-26. Модуль скопированного метода сохраняется

Модуль A (`main.goat` модуля `a@1`):

```goat
external obj = {
    var internal = 7;
    external read = func() { return internal; };
};
```

Модуль B (`main.goat`):

```goat
const A = import "a@1";
var copy = A.obj.flat();
copy.extra = 1;
print(copy.read());
```

read() даёт 7. Результат принадлежит B, но перенесённый код read сохраняет исходный файл и модуль A и может читать перенесённое локальное для файла поле. Внешнее copy.internal из B по-прежнему запрещено. Смена владельца объекта не означает смену модуля функции или каждого поля.

<a id="ac-27"></a>

## AC-27. Защищённый доступ через границу модулей

Модуль A (`main.goat` модуля `a@1`):

```goat
external Base = {
    protected var x = 10;
    private var secret = 20;
};
```

Модуль B (`main.goat`):

```goat
const A = import "a@1";
var child = A.Base -> {
    public read = func() { return A.Base.x; };
};
print(child.read());
```

10. Модуль B не видит protected x произвольным внешним чтением, но метод потомка имеет право на A.Base.x. Аналогичное чтение A.Base.secret новым методом потомка запрещено.

<a id="ac-28"></a>

## AC-28. Публичность не отменяет константность

Модуль A (`main.goat` модуля `a@1`):

```goat
external obj = {
    external var writable = 1;
    external fixed = 2;
};
```

Модуль B (`main.goat`):

```goat
const A = import "a@1";
A.obj.writable = 3;
print(A.obj.fixed);
```

Запись разрешена, чтение даёт 2. Отдельная попытка A.obj.fixed = 4 запрещена из-за константности. Попытка добавить A.obj.newField = 5 запрещена из-за принадлежности объекта A. Это разные основания отказа, их нужно различать в диагностике.




## Анализ доступа: статический запрет и динамическая проверка

Статический отрицательный пример (не запускается даже с `--optimize none`):

```goat
const box = { private var secret = 42; };
try box["secret"] += 1; catch(e) println(e); // ошибка компиляции read/write
```

Эквивалент с неизвестным получателем оставляет проверку в VM:

```goat
const box = { private var secret = 42; };
const read = func(value) { return value.secret; };
try read(box); catch(e) println(e); // ACCESS_DENIED, оба уровня оптимизации
```

Модификатор локальной переменной не запрещает безопасное нативное исполнение:

```goat
public add = func(a, b) { private var result = a + b; return result; };
println(add(2, 3)); // 5, работает с --native required
```

Этап G (#168) различает разрешённый, запрещённый и неизвестный доступ. Прямые закрытые поля объекта, связанного неизменяемым лексическим именем (в том числе через цепочку константных псевдонимов), дают ошибку компиляции с операцией, именем и позицией объявления. Это действует и при `--optimize none`, и внутри `try`. Динамический получатель или ключ, результат фабрики/clone/flat, захваченные из другой функции константы, приоритетные приватные слоты, наследуемый поиск и зависящие от вызова protected-права сохраняют проверку VM. Захваченный const нельзя считать неизменным после flat: его перенесённая ячейка может быть изменена. Неизвестность не означает отсутствия свойства.

Для программ с модификаторами оптимизатор пока сворачивает только замкнутые скалярные выражения: без чтения имён, вызовов, свойств и удаления областей. Он сохраняет модификаторы, идентификаторы объявлений и захваты; в частности, константное поле исходного объекта не превращает чтение изменяемой ячейки flat в литерал. Это ограничение оптимизации, а не отключение проверки прав.

Нативный контракт содержит причину отказа `access`. Доказанные скалярные локальные операции, включая локальные объявления с модификатором, и доказанные строковые получатели могут исполняться нативно. Доступы, которым нужны права VM, остаются в VM; неизвестные пользовательские объекты не получают нативного обхода проверки. При отсутствии безопасных функций `--native required` сообщает `Access checks require VM execution`. Этот режим требует наличия нативной реализации в программе, а не нативности каждого вызова. Скопированные функции не переиспользуют исходные нативные дескрипторы; вызовы методов сохраняют существующий VM-путь.

Проверки включают статические отрицательные случаи, динамические аналоги, русский/английский текст ошибок, оба уровня оптимизации, восстановленный исходник и байткод, безопасные нативные вызовы и отказ required. Проверяются откат при недоступном C-компиляторе и повтор в VM после ограничения глубины: аргументы с побочными эффектами вычисляются один раз, после вызовов закрытые поля остаются закрытыми. Многофайловый набор ниже проверяет те же правила через компилятор исходников, проверенные архивы, `include` и `import`, не подменяя загрузку искусственными идентификаторами областей.

## Проверки на реальных файлах и модулях (#218)

В AC-01–AC-24 `public` означает доступ из другого файла **того же модуля**, а не из импортирующего модуля. Минимальное разделение: `public obj = { ... };` в `objects.goat`, затем `const objects = include "objects.goat";` и `objects.obj` в потребителе. При `import` и внешняя привязка, и каждое читаемое вложенное поле должны допускать доступ `external` (если нет отдельного права private/protected). AC-25–AC-28 показывают эту границу явно. Каждый модуль использует описание `.gmod` с `formatVersion: 1`, именем/версией и `main: "main.goat"`; описание A задаёт имя `a`, версию `1`.

Точка запуска интеграционных проверок — [`test/src/functional/module_multifile_access_scenarios.c`](../../../test/src/functional/module_multifile_access_scenarios.c), использующая существующий стенд исполнения модулей:

| Примеры | Фикстуры | Предмет проверки |
| --- | --- | --- |
| AC-01–AC-10 | `test/data/module-access/scenarios.json` (`basic`) | Объявления, отказ анализатора, динамический запрет, замыкания, домены private/protected, остановка поиска |
| AC-11–AC-22 | `test/data/module-access/scenarios.json` (`copy`) | clone/flat через реальные файлы/модули, отдельные и общие ячейки, поверхностные копии, идентичность методов, графы прототипов |
| AC-23–AC-28 | `test/data/module-access/scenarios.json` (`boundary`) | Контекст наблюдателя, массивы, фабрики, происхождение скопированного метода, protected между модулями, константность и расширение |

Они дополняют существующие наборы проверки происхождения/переэкспорта функций и доступа при присваивании. Запрет сохраняется при переходе границы файла/модуля и копировании; положительные случаи сохраняют значения, идентичность и побочные эффекты. Возврат или переэкспорт функции сохраняет контекст её объявления, а не права вызывающего кода.
