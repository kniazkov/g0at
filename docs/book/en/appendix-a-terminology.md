# Appendix A. Terminology

[Contents](index.md) · [Русский](../ru/appendix-a-terminology.md) · [Previous](29-performance-measurement.md) · [Next](appendix-b-language.md)

Revision 2. Implementation described: [commit 8d1fe86, including `println`](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0).

This glossary fixes terminology used in the book. English structure and flag names remain unchanged in source references. Sharing the word “summary” does not make a declaration summary and a specialization proof the same structure.

| Русский | English | Meaning |
| --- | --- | --- |
| Лексема | Lexeme | A source fragment recognized as one language unit. |
| Токен | Token | A record of a lexeme, its kind, and position. |
| Синтаксический анализ | Parsing | Building program structure from tokens. |
| Свёртка правила | Reduction | Replacing a token-list segment with one parsed node. |
| Абстрактное синтаксическое дерево, AST | Abstract syntax tree, AST | A tree of program constructs without original formatting. |
| Область видимости | Scope | A lexical region determining which declaration a name denotes. |
| Связывание имени | Name binding | Associating a name use with its declaration. |
| Контекст | Context | Runtime state of a region: data, links, and control. |
| Замыкание | Closure | A function with a retained environment for accessing outer data. |
| Захват | Capture | A function dependency on a declaration outside its local scope. |
| Арена | Arena | Storage for many objects released together. |
| Владение | Ownership | Responsibility for retaining and releasing a resource. |
| Счётчик ссылок | Reference count | The number of retained references tracked by an object. |
| Сборка мусора | Garbage collection | Reclaiming objects unreachable from runtime roots. |
| Байткод | Bytecode | Instructions for a software virtual machine. |
| Код операции | Opcode | A numeric selection of an instruction handler. |
| Стек данных | Data stack | A value sequence accessed primarily at its top. |
| Абстрактное значение | Abstract value | A description of possible concrete values. |
| Абстрактная интерпретация | Abstract interpretation | Computing over value descriptions rather than a concrete run. |
| Решётка | Lattice | A partially ordered domain with join and meet. |
| Объединение | Join | An approximation covering the possibilities of both inputs. |
| Пересечение | Meet | A description of values allowed by both inputs. |
| TOP / ⊤ | TOP / ⊤ | Any value; no more precise information. |
| BOTTOM / ⊥ | BOTTOM / ⊥ | No possible normal value; not the null object. |
| Передаточная функция | Transfer function | A rule for a node to transform abstract state or a result. |
| Сводка | Summary | Accumulated information about a declaration or function. |
| Специализация | Specialization | A function-analysis variant for ordered parameter types. |
| Граф вызовов | Call graph | Possible-call relationships between specializations. |
| Сильно связная компонента, SCC | Strongly connected component, SCC | A group in which every vertex is reachable from every other. |
| Неподвижная точка | Fixed point | A state unchanged by the next computation step. |
| Побочный эффект | Side effect | An observable action beyond a returned value. |
| Чистота | Purity | Proven absence of tracked external effects; not guaranteed termination. |
| Недостижимость | Unreachability | Inability to reach a region through the paths under consideration. |
| Бэкенд | Backend | The subsystem producing target code. |
| Понижение представления | Lowering | Translation of a high-level construct into simpler actions. |
| Нативное исполнение | Native execution | Executing compiled machine code for functions. |
| ABI | Application binary interface | A data-layout and calling contract between compiled components. |
| Адаптер | Adapter | A wrapper between the uniform VM interface and a typed C function. |
| Возврат к VM | VM fallback | Switching to bytecode execution under permitted conditions. |
| Раздельная компиляция | Separate compilation | Saving a prepared program for later execution. |
| Контрольная сумма | Checksum | A number detecting changed contents; not an author signature. |
| Регрессия | Regression | Reappearance of a previously fixed defect. |
| Медиана | Median | The middle element of a sorted odd-sized sample. |

Terms refer to the described implementation. For example, “separate compilation” does not mean a system of imported source modules, and “purity” does not broaden the numeric ABI. See chapters 14–20 and 23–26.
