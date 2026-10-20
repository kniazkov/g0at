# Модификаторы доступа: примеры и угловые случаи

[Оглавление](index.md) · [English](../en/access-control-examples.md) · [Правила](appendix-i-access-control.md) · [План](access-control-implementation-plan.md)

Это примеры согласованного проекта. Этапы A–E реализуют объявления, проверки доступа VM и адаптацию методов/захватов clone и flat; фильтрация наблюдения, загрузка модулей и статическая диагностика доступа относятся к следующим этапам. Каждый пример независим; ожидаемые результаты описаны словами, вызовы `print` не подразумевают автоматические переводы строк. Отрицательные случаи проверяются отдельно, в том числе статически и при динамически неизвестном получателе. Все примеры относятся к стандартным встроенным `clone` и `flat`, а не пользовательским переопределениям.

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
const external = func() { return this.x; };
var obj = {
    private var x = 10;
    public read = external;
};
var copy = obj.clone();
print(copy.read());
```

Ошибка доступа. Ни присваивание external свойству, ни clone не выдаёт права на private x. Тот же результат ожидается после flat().

<a id="ac-05"></a>

## AC-05. Перенос метода обычным присваиванием

```goat
var a = {
    private var x = 10;
    public read = func() { return this.x; };
};
var b = {
    private var x = 20;
};
b.read = a.read;
print(b.read());
```

Ошибка доступа к приватному x объекта b. Перенос ссылки на метод не адаптирует его права. Это отличается от специальной адаптации методов при clone/flat.

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

Модуль A:

```goat
public make = func() {
    return {
        public var x = 1;
        var internal = 7;
    };
};
```

Модуль B:

```goat
var obj = A.make();
obj.x = 2;
var copy = obj.clone();
copy.extra = 3;
var child = obj -> { var own = 4; };
```

Первый блок — модуль A, второй — модуль B. Предполагается, что B уже получил публичную ссылку A; синтаксис импорта здесь не задаётся. obj принадлежит A, поэтому отдельная попытка obj.extra = 3 запрещена. copy и child принадлежат B. Чтение obj.internal или copy.internal из B запрещено: модуль объявления поля остаётся A.

<a id="ac-26"></a>

## AC-26. Модуль скопированного метода сохраняется

Модуль A:

```goat
public obj = {
    var internal = 7;
    public read = func() { return internal; };
};
```

Модуль B:

```goat
var copy = A.obj.flat();
copy.extra = 1;
print(copy.read());
```

read() даёт 7. Результат принадлежит B, но перенесённый код read остаётся кодом модуля A и может читать перенесённое поле, объявленное в A. Внешнее copy.internal из B по-прежнему запрещено. Смена владельца объекта не означает смену модуля функции или каждого поля.

<a id="ac-27"></a>

## AC-27. Защищённый доступ через границу модулей

Модуль A:

```goat
public Base = {
    protected var x = 10;
    private var secret = 20;
};
```

Модуль B:

```goat
var child = A.Base -> {
    public read = func() { return A.Base.x; };
};
print(child.read());
```

10. Модуль B не видит protected x произвольным внешним чтением, но метод потомка имеет право на A.Base.x. Аналогичное чтение A.Base.secret новым методом потомка запрещено.

<a id="ac-28"></a>

## AC-28. Публичность не отменяет константность

Модуль A:

```goat
public obj = {
    public var writable = 1;
    public fixed = 2;
};
```

Модуль B:

```goat
A.obj.writable = 3;
print(A.obj.fixed);
```

Запись разрешена, чтение даёт 2. Отдельная попытка A.obj.fixed = 4 запрещена из-за константности. Попытка добавить A.obj.newField = 5 запрещена из-за принадлежности объекта A. Это разные основания отказа, их нужно различать в диагностике.


