# Model and built-in methods

The current implementation separates three responsibilities:

- `src/model/<type>.c` implements values: storage, lifetime, virtual operations and constructors.
- `src/model/<type>_proto.c` implements the corresponding prototype object and its property lookup. This applies to Array, String, Boolean, Character, Integer, Real and Function. `number_proto.c` contains Number, the shared numeric ancestor; it has no separate value representation. `root_object.c` already contains the Object prototype. Null remains a singleton value in `null.c`.
- `src/builtins/` implements Goat-call adapters: receiver and argument validation, built-in descriptors, abstract interpretation and calls into model operations.

Array follows the same separation as String. `builtins/array_methods.c` exposes methods through `array_proto.c`; these methods are not added to the global built-in registry. Thus Array and String may have distinct methods with the same name.

Array algorithms live in `model/array_operations.c`, with their interface in `model/array.h`. Callers validate receiver types and bounds. Arguments are borrowed and successful operation results own a reference. Storage layout and buffer helpers are private to the model through `model/array_internal.h`; call adapters do not manipulate the buffer or reference counts directly. These headers are interpreter internals, not native ABI headers.

# Модель и встроенные методы

В текущей реализации разделены три обязанности:

- `src/model/<type>.c` реализует значения: хранение, время жизни, виртуальные операции и конструкторы.
- `src/model/<type>_proto.c` реализует соответствующий объект-прототип и поиск его свойств. Так устроены Array, String, Boolean, Character, Integer, Real и Function. `number_proto.c` содержит общего числового предка Number, отдельного представления значений у него нет. `root_object.c` уже выделен для прототипа Object. Null остаётся единственным значением в `null.c`.
- `src/builtins/` содержит обвязку вызовов из Goat: проверку получателя и аргументов, дескрипторы встроенных функций, абстрактную интерпретацию и обращения к операциям модели.

Array разделён по тому же принципу, что и String. Методы из `builtins/array_methods.c` доступны через `array_proto.c`, но не добавлены в глобальный реестр встроенных функций. Поэтому у Array и String могут быть разные методы с одинаковыми именами.

Алгоритмы массивов находятся в `model/array_operations.c`, их интерфейс — в `model/array.h`. Вызывающая сторона проверяет тип получателя и границы индексов. Аргументы заимствуются, успешный результат операции владеет ссылкой. Структура хранения и вспомогательные функции буфера доступны внутри модели через `model/array_internal.h`; обвязка вызовов не управляет буфером и счётчиками ссылок напрямую. Эти заголовки относятся к внутреннему устройству интерпретатора, а не к нативному ABI.
