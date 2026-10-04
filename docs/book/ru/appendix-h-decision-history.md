# Приложение З. История решений

[Оглавление](index.md) · [English](../en/appendix-h-decision-history.md) · [Назад](appendix-g-implementation-boundaries.md)

Редакция 2. Описываемая реализация: [commit 8d1fe86, с функцией `println`](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0).

Это указатель изменений, а не восстановление мотивов автора по названиям веток. Ссылки ведут к обсуждению и diff конкретных PR. Текущие правила следует брать из кода и глав книги: исторический текст мог описывать промежуточный этап. Номера ниже подтверждаются merge-коммитами основной ветки.

| PR | Зафиксированное изменение и место в книге |
| --- | --- |
| [#19](https://github.com/kniazkov/g0at/pull/19), [#20](https://github.com/kniazkov/g0at/pull/20) | Безопасность служебной памяти и рост/UTF-8 строк; главы 4, 11. |
| [#23](https://github.com/kniazkov/g0at/pull/23), [#24](https://github.com/kniazkov/g0at/pull/24) | Коллектор наблюдений и текстовый формат анализа; главы 15, 27. |
| [#26](https://github.com/kniazkov/g0at/pull/26), [#27](https://github.com/kniazkov/g0at/pull/27) | Законы решётки и независимые абстрактные состояния; главы 14, 15. |
| [#28](https://github.com/kniazkov/g0at/pull/28), [#29](https://github.com/kniazkov/g0at/pull/29) | Анализ if и отметки недостижимости; главы 16, 21. |
| [#30](https://github.com/kniazkov/g0at/pull/30), [#35](https://github.com/kniazkov/g0at/pull/35) | Семантика сложения и результат операции с признаком исключения; главы 9, 12. |
| [#31](https://github.com/kniazkov/g0at/pull/31), [#32](https://github.com/kniazkov/g0at/pull/32), [#33](https://github.com/kniazkov/g0at/pull/33), [#34](https://github.com/kniazkov/g0at/pull/34) | Объект ошибок, инструкции и разбор try/catch; глава 12. |
| [#36](https://github.com/kniazkov/g0at/pull/36), [#37](https://github.com/kniazkov/g0at/pull/37), [#42](https://github.com/kniazkov/g0at/pull/42), [#43](https://github.com/kniazkov/g0at/pull/43), [#44](https://github.com/kniazkov/g0at/pull/44), [#45](https://github.com/kniazkov/g0at/pull/45), [#46](https://github.com/kniazkov/g0at/pull/46), [#47](https://github.com/kniazkov/g0at/pull/47), [#48](https://github.com/kniazkov/g0at/pull/48) | Вычитание, унарные, арифметические, сравнительные, логические и побитовые операции; приложение Б. |
| [#38](https://github.com/kniazkov/g0at/pull/38), [#39](https://github.com/kniazkov/g0at/pull/39), [#40](https://github.com/kniazkov/g0at/pull/40), [#41](https://github.com/kniazkov/g0at/pull/41), [#54](https://github.com/kniazkov/g0at/pull/54) | Скрипты CI, стиль, Doxygen, Linux Release и сборка только на C; глава 28. |
| [#49](https://github.com/kniazkov/g0at/pull/49), [#50](https://github.com/kniazkov/g0at/pull/50), [#52](https://github.com/kniazkov/g0at/pull/52) | Анализ вызовов, встроенные функции, математика, int и input; главы 13, 17. |
| [#55](https://github.com/kniazkov/g0at/pull/55), [#56](https://github.com/kniazkov/g0at/pull/56), [#57](https://github.com/kniazkov/g0at/pull/57) | Сводки, сигнатуры и типы результатов функций; глава 17. |
| [#58](https://github.com/kniazkov/g0at/pull/58), [#59](https://github.com/kniazkov/g0at/pull/59), [#60](https://github.com/kniazkov/g0at/pull/60) | Граф вызовов, прямая и взаимная рекурсия; глава 18. |
| [#61](https://github.com/kniazkov/g0at/pull/61), [#62](https://github.com/kniazkov/g0at/pull/62) | Локальные эффекты и доказательство чистоты; глава 19. |
| [#63](https://github.com/kniazkov/g0at/pull/63), [#64](https://github.com/kniazkov/g0at/pull/64), [#65](https://github.com/kniazkov/g0at/pull/65), [#66](https://github.com/kniazkov/g0at/pull/66) | Контракт C, выражения, полные тела и отображение специализаций; главы 20, 27. |
| [#67](https://github.com/kniazkov/g0at/pull/67), [#68](https://github.com/kniazkov/g0at/pull/68) | Замены AST и свёртка abs; глава 21. |
| [#70](https://github.com/kniazkov/g0at/pull/70), [#71](https://github.com/kniazkov/g0at/pull/71), [#72](https://github.com/kniazkov/g0at/pull/72), [#73](https://github.com/kniazkov/g0at/pull/73), [#74](https://github.com/kniazkov/g0at/pull/74) | Нативный контракт, контекст генерации, исходные поддеревья, состав модуля и минимальные функции; главы 20–22. |
| [#75](https://github.com/kniazkov/g0at/pull/75), [#76](https://github.com/kniazkov/g0at/pull/76), [#77](https://github.com/kniazkov/g0at/pull/77), [#78](https://github.com/kniazkov/g0at/pull/78), [#79](https://github.com/kniazkov/g0at/pull/79), [#80](https://github.com/kniazkov/g0at/pull/80), [#81](https://github.com/kniazkov/g0at/pull/81) | Числовой C, сравнения/ветвления, локальные значения, статические вызовы, рекурсия, модуль и целые helper-функции; глава 22. |
| [#82](https://github.com/kniazkov/g0at/pull/82) | Числовой ABI и адаптеры; глава 23. |
| [#83](https://github.com/kniazkov/g0at/pull/83), [#84](https://github.com/kniazkov/g0at/pull/84), [#85](https://github.com/kniazkov/g0at/pull/85) | Сборка библиотек Linux/Windows и загрузчик; глава 24. |
| [#86](https://github.com/kniazkov/g0at/pull/86), [#87](https://github.com/kniazkov/g0at/pull/87), [#88](https://github.com/kniazkov/g0at/pull/88) | CALL-диспетчеризация, нативный конвейер с повтором и режимы исполнения; глава 25. |
| [#89](https://github.com/kniazkov/g0at/pull/89) | Проверка замен AST по доказательствам специализаций; главы 21, 22. |
| [#90](https://github.com/kniazkov/g0at/pull/90) | Раздельная компиляция и исполнение; глава 26. |
| [#91](https://github.com/kniazkov/g0at/pull/91) | Тест производительности дерева вызовов; глава 29. |
| [#92](https://github.com/kniazkov/g0at/pull/92) | Проверки повреждённых binary через общий декодер в памяти; глава 26. |
| [#93](https://github.com/kniazkov/g0at/pull/93) | Статическая линковка MinGW runtime в генерируемые DLL; глава 24. |
| [#97](https://github.com/kniazkov/g0at/pull/97) | println с исполнительными и абстрактными проверками; глава 13 и приложение Г. |

<a id="section-h-1"></a>

## З.1. История самой книги

[PR #94](https://github.com/kniazkov/g0at/pull/94) зафиксировал план; [#95](https://github.com/kniazkov/g0at/pull/95) добавил первую главу; [#96](https://github.com/kniazkov/g0at/pull/96) — часть I. Далее появились [часть II, #98](https://github.com/kniazkov/g0at/pull/98), [часть III, #99](https://github.com/kniazkov/g0at/pull/99), [часть IV, #100](https://github.com/kniazkov/g0at/pull/100) и [часть V, #101](https://github.com/kniazkov/g0at/pull/101).

Эти записи о документации относятся и к времени после описываемого commit реализации. Добавление println изменило доступный язык, поэтому книга отмечена редакцией 2 и ссылается на commit 8d1fe86. Последующее добавление глав само по себе не расширяет возможности интерпретатора.

Для ссылки на устойчивый технический факт полезнее ссылка на закреплённый исходник или раздел книги в конкретном commit. PR полезен там, где требуется показать сам переход: какой код появился, какие проверки были добавлены и что изменилось относительно родительского состояния.
