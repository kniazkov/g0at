# Порядок внедрения модульной системы Goat

Источники: [modules.md](book/ru/modules.md), [английская версия](book/en/modules.md), [правила доступа](book/ru/appendix-i-access-control.md), [примеры](book/ru/access-control-examples.md), [план доступа](book/ru/access-control-implementation-plan.md).

Порядок является рекомендуемым топологическим планом; независимые задачи допускают параллельную работу. Пункт закрывается после выполнения критериев приёмки, тестов и актуализации документации.

## Упорядоченный чеклист
- [ ] 01. [#178](https://github.com/kniazkov/g0at/issues/178) — Строгий разбор JSON описаний модулей
- [ ] 02. [#179](https://github.com/kniazkov/g0at/issues/179) — Проверка имён и версий модулей
- [ ] 03. [#180](https://github.com/kniazkov/g0at/issues/180) — Канонический JSON для хэширования
- [ ] 04. [#181](https://github.com/kniazkov/g0at/issues/181) — Основной SHA-256 модуля
- [ ] 05. [#182](https://github.com/kniazkov/g0at/issues/182) — Проверка ZIP-контейнера .gbin
- [ ] 06. [#184](https://github.com/kniazkov/g0at/issues/184) — Проверка переносимого байткода
- [ ] 07. [#185](https://github.com/kniazkov/g0at/issues/185) — Единый API проверки .gbin
- [ ] 08. [#183](https://github.com/kniazkov/g0at/issues/183) — Детерминированная запись .gbin
- [ ] 09. [#221](https://github.com/kniazkov/g0at/issues/221) — Информационные метаданные и сведения об авторе
- [ ] 10. [#186](https://github.com/kniazkov/g0at/issues/186) — Чтение .gmod и построение набора исходников
- [ ] 11. [#193](https://github.com/kniazkov/g0at/issues/193) — Раздельные области видимости исходных файлов
- [ ] 12. [#187](https://github.com/kniazkov/g0at/issues/187) — Статическое извлечение импортов и версий
- [ ] 13. [#188](https://github.com/kniazkov/g0at/issues/188) — Поиск модулей в каталогах
- [ ] 14. [#237](https://github.com/kniazkov/g0at/issues/237) — Точные правила путей GOAT_MODULE_PATH
- [ ] 15. [#189](https://github.com/kniazkov/g0at/issues/189) — Итеративная проверка графа зависимостей
- [ ] 16. [#190](https://github.com/kniazkov/g0at/issues/190) — Детерминированная диагностика зависимостей
- [ ] 17. [#238](https://github.com/kniazkov/g0at/issues/238) — Независимость версий зависимостей библиотеки
- [ ] 18. [#191](https://github.com/kniazkov/g0at/issues/191) — Интеграция сборки .gmod в CLI
- [ ] 19. [#234](https://github.com/kniazkov/g0at/issues/234) — Диагностика исходников в скомпилированных модулях
- [ ] 20. [#192](https://github.com/kniazkov/g0at/issues/192) — Загрузка снимка графа модулей в память
- [ ] 21. [#236](https://github.com/kniazkov/g0at/issues/236) — Поведение при нехватке памяти при загрузке
- [ ] 22. [#235](https://github.com/kniazkov/g0at/issues/235) — Идентичность экземпляров модулей и копий архива
- [ ] 23. [#194](https://github.com/kniazkov/g0at/issues/194) — Оператор include для исходных файлов
- [ ] 24. [#195](https://github.com/kniazkov/g0at/issues/195) — Оператор import для готовых модулей
- [ ] 25. [#196](https://github.com/kniazkov/g0at/issues/196) — Ленивая однократная инициализация файлов
- [ ] 26. [#225](https://github.com/kniazkov/g0at/issues/225) — Проверка семантики незавершённых областей
- [ ] 27. [#226](https://github.com/kniazkov/g0at/issues/226) — Проверка прототипов после ошибки инициализации
- [ ] 28. [#197](https://github.com/kniazkov/g0at/issues/197) — Сохранение ошибок инициализации
- [ ] 29. [#229](https://github.com/kniazkov/g0at/issues/229) — Точные правила распространения исключений и finally
- [ ] 30. [#239](https://github.com/kniazkov/g0at/issues/239) — Инициализация: таймаут и идентичность исключения
- [ ] 31. [#228](https://github.com/kniazkov/g0at/issues/228) — Переэкспорт и происхождение функций
- [ ] 32. [#204](https://github.com/kniazkov/g0at/issues/204) — Новые границы доступа файлов и модулей
- [ ] 33. [#227](https://github.com/kniazkov/g0at/issues/227) — Права и порядок вычисления присваиваний
- [ ] 34. [#205](https://github.com/kniazkov/g0at/issues/205) — Экспорт интерфейса и анализатор потребителя
- [ ] 35. [#231](https://github.com/kniazkov/g0at/issues/231) — Проверка аналитических контрактов функций
- [ ] 36. [#206](https://github.com/kniazkov/g0at/issues/206) — Встроенные __main__ и __dir__
- [ ] 37. [#207](https://github.com/kniazkov/g0at/issues/207) — Верхнеуровневый return и exit
- [ ] 38. [#230](https://github.com/kniazkov/g0at/issues/230) — Полная семантика exit и очистки
- [ ] 39. [#208](https://github.com/kniazkov/g0at/issues/208) — Метаданные платформенных нативных вариантов
- [ ] 40. [#233](https://github.com/kniazkov/g0at/issues/233) — Согласованность нативных метаданных с байткодом
- [ ] 41. [#209](https://github.com/kniazkov/g0at/issues/209) — Нативный ABI и экспортируемые специализации
- [ ] 42. [#232](https://github.com/kniazkov/g0at/issues/232) — Проверка нативных специализаций по каждому вызову
- [ ] 43. [#211](https://github.com/kniazkov/g0at/issues/211) — Интеграция нативной сборки с .gmod
- [ ] 44. [#210](https://github.com/kniazkov/g0at/issues/210) — Ленивая загрузка нативных библиотек
- [ ] 45. [#212](https://github.com/kniazkov/g0at/issues/212) — goat-inspect: просмотр метаданных
- [ ] 46. [#223](https://github.com/kniazkov/g0at/issues/223) — Машиночитаемый вывод goat-inspect
- [ ] 47. [#224](https://github.com/kniazkov/g0at/issues/224) — Инспекция и объединение без транзитивных зависимостей
- [ ] 48. [#213](https://github.com/kniazkov/g0at/issues/213) — Переносимый запуск дочерних процессов
- [ ] 49. [#214](https://github.com/kniazkov/g0at/issues/214) — goat-inspect: безопасная пробная загрузка
- [ ] 50. [#222](https://github.com/kniazkov/g0at/issues/222) — Конфликты информационных метаданных при goat-merge
- [ ] 51. [#215](https://github.com/kniazkov/g0at/issues/215) — goat-merge: объединение вариантов
- [ ] 52. [#216](https://github.com/kniazkov/g0at/issues/216) — Переносимый API потоков в lib
- [ ] 53. [#217](https://github.com/kniazkov/g0at/issues/217) — Сериализация инициализации между потоками
- [ ] 54. [#218](https://github.com/kniazkov/g0at/issues/218) — Многофайловые тесты модели доступа
- [ ] 55. [#219](https://github.com/kniazkov/g0at/issues/219) — Сквозные тесты сборки и поставки
- [ ] 56. [#220](https://github.com/kniazkov/g0at/issues/220) — Актуализация русской и английской книги

## Матрица требований modules.md

| № | Требование | Основная задача |
|---:|---|---|
| 1 | Функция печатает с правами своего файла | [#228](https://github.com/kniazkov/g0at/issues/228) |
| 2 | Callback не получает права вызывающей функции | [#228](https://github.com/kniazkov/g0at/issues/228) |
| 3 | Возвращённое замыкание сохраняет права и захваты | [#228](https://github.com/kniazkov/g0at/issues/228) |
| 4 | Хранение функции как метода не меняет её файл | [#228](https://github.com/kniazkov/g0at/issues/228) |
| 5 | Переэкспорт сохраняет права исходной функции | [#228](https://github.com/kniazkov/g0at/issues/228) |
| 6 | Экспорт объекта не экспортирует его поля автоматически | [#228](https://github.com/kniazkov/g0at/issues/228) |
| 7 | Возврат объекта не передаёт права функции | [#228](https://github.com/kniazkov/g0at/issues/228) |
| 8 | Константность ссылки не делает массив неизменяемым | [#228](https://github.com/kniazkov/g0at/issues/228) |
| 9 | Присваивание сохраняет происхождение поля | [#228](https://github.com/kniazkov/g0at/issues/228) |
| 10 | Права поля и новой функции независимы | [#228](https://github.com/kniazkov/g0at/issues/228) |
| 11 | Переэкспорт переменной создаёт отдельную ячейку | [#228](https://github.com/kniazkov/g0at/issues/228) |
| 12 | Область внутреннего файла можно явно переэкспортировать | [#228](https://github.com/kniazkov/g0at/issues/228) |
| 13 | Индекс и точка проверяют одинаковые права | [#227](https://github.com/kniazkov/g0at/issues/227) |
| 14 | Ошибка чтения в составном присваивании предшествует правой части | [#227](https://github.com/kniazkov/g0at/issues/227) |
| 15 | Обычное присваивание проверяет запись до правой части | [#227](https://github.com/kniazkov/g0at/issues/227) |
| 16 | Составное присваивание заранее проверяет возможность записи | [#227](https://github.com/kniazkov/g0at/issues/227) |
| 17 | Используется значение, прочитанное до правой части | [#227](https://github.com/kniazkov/g0at/issues/227) |
| 18 | Исключение отменяет итоговую запись, но не побочные эффекты | [#227](https://github.com/kniazkov/g0at/issues/227) |
| 19 | Объект назначения запоминается до правой части | [#227](https://github.com/kniazkov/g0at/issues/227) |
| 20 | Вычисленный ключ также запоминается | [#227](https://github.com/kniazkov/g0at/issues/227) |
| 21 | Правая часть создала недоступное поле | [#227](https://github.com/kniazkov/g0at/issues/227) |
| 22 | Отсутствующее поле не создаётся заранее | [#227](https://github.com/kniazkov/g0at/issues/227) |
| 23 | Доступное поле, созданное правой частью, обновляется | [#227](https://github.com/kniazkov/g0at/issues/227) |
| 24 | Индекс массива обозначает позицию, а не прежний элемент | [#227](https://github.com/kniazkov/g0at/issues/227) |
| 25 | Исчезнувшая позиция вызывает ошибку окончательной записи | [#227](https://github.com/kniazkov/g0at/issues/227) |
| 26 | Внутренний catch позволяет успешно завершить инициализацию | [#229](https://github.com/kniazkov/g0at/issues/229) |
| 27 | Перехват ошибки подключаемого файла не блокирует вызывающий файл | [#229](https://github.com/kniazkov/g0at/issues/229) |
| 28 | Неперехваченное исключение прерывает всю активную цепочку | [#229](https://github.com/kniazkov/g0at/issues/229) |
| 29 | Ошибка функции после инициализации не меняет состояние файла | [#229](https://github.com/kniazkov/g0at/issues/229) |
| 30 | Неудача потребителя не отменяет готовую зависимость | [#229](https://github.com/kniazkov/g0at/issues/229) |
| 31 | Повторно выбрасывается то же значение без клонирования | [#229](https://github.com/kniazkov/g0at/issues/229) |
| 32 | Распространение одной ошибки сохраняет общую ссылку | [#229](https://github.com/kniazkov/g0at/issues/229) |
| 33 | Перехват с новой ошибкой разделяет сохранённые значения | [#229](https://github.com/kniazkov/g0at/issues/229) |
| 34 | Исключение из finally становится окончательной ошибкой | [#229](https://github.com/kniazkov/g0at/issues/229) |
| 35 | Finally не повторяется при повторном подключении | [#229](https://github.com/kniazkov/g0at/issues/229) |
| 36 | Преждевременное чтение можно перехватить | [#225](https://github.com/kniazkov/g0at/issues/225) |
| 37 | Запись в ещё не инициализированное объявление запрещена | [#225](https://github.com/kniazkov/g0at/issues/225) |
| 38 | Наблюдение пропускает ещё не готовые объявления | [#225](https://github.com/kniazkov/g0at/issues/225) |
| 39 | Копирование незавершённой области запрещено | [#225](https://github.com/kniazkov/g0at/issues/225) |
| 40 | Наследование от незавершённой области разрешено | [#225](https://github.com/kniazkov/g0at/issues/225) |
| 41 | Неудачный прототип не блокирует собственные поля потомка | [#226](https://github.com/kniazkov/g0at/issues/226) |
| 42 | Flat не возвращает частичный результат при неуспешном прототипе | [#226](https://github.com/kniazkov/g0at/issues/226) |
| 43 | Clone потомка сохраняет ссылку на неуспешный прототип | [#226](https://github.com/kniazkov/g0at/issues/226) |
| 44 | Flat запрещён и при ещё не завершившейся инициализации прототипа | [#226](https://github.com/kniazkov/g0at/issues/226) |
| 45 | Проверяется весь граф прототипов | [#226](https://github.com/kniazkov/g0at/issues/226) |
| 46 | Разные версии — разные сущности и границы доступа | [#235](https://github.com/kniazkov/g0at/issues/235) |
| 47 | Физические копии одной сборки не создают разные экземпляры | [#235](https://github.com/kniazkov/g0at/issues/235) |
| 48 | Нативные части разных копий автоматически не объединяются | [#235](https://github.com/kniazkov/g0at/issues/235) |
| 49 | Найденный, но нечитаемый файл останавливает поиск | [#237](https://github.com/kniazkov/g0at/issues/237) |
| 50 | Невозможность проверить каталог не равна отсутствию файла | [#237](https://github.com/kniazkov/g0at/issues/237) |
| 51 | Явный относительный module-dir привязан к рабочему каталогу | [#237](https://github.com/kniazkov/g0at/issues/237) |
| 52 | Относительные пути окружения фиксируются при запуске | [#237](https://github.com/kniazkov/g0at/issues/237) |
| 53 | Пустые элементы списка игнорируются | [#237](https://github.com/kniazkov/g0at/issues/237) |
| 54 | Отсутствие переменной окружения не является ошибкой | [#237](https://github.com/kniazkov/g0at/issues/237) |
| 55 | Диагностика различает отсутствие и неисправность | [#237](https://github.com/kniazkov/g0at/issues/237) |
| 56 | Неиспользуемая запись описания даёт предупреждение | [#238](https://github.com/kniazkov/g0at/issues/238) |
| 57 | Импорт в недостижимой ветке считается использованием | [#238](https://github.com/kniazkov/g0at/issues/238) |
| 58 | Явная другая версия не использует версию из описания | [#238](https://github.com/kniazkov/g0at/issues/238) |
| 59 | Внешние зависимости не встраиваются в потребителя | [#238](https://github.com/kniazkov/g0at/issues/238) |
| 60 | Приложение не переопределяет зависимости библиотеки | [#238](https://github.com/kniazkov/g0at/issues/238) |
| 61 | Содержимое модулей удерживается в оперативной памяти | [#192](https://github.com/kniazkov/g0at/issues/192) |
| 62 | Нехватка памяти завершает процесс | [#192](https://github.com/kniazkov/g0at/issues/192) |
| 63 | Inspect не требует внешних зависимостей модуля | [#224](https://github.com/kniazkov/g0at/issues/224) |
| 64 | Merge не требует файлов внешних зависимостей | [#224](https://github.com/kniazkov/g0at/issues/224) |
| 65 | Merge не загружает нативные библиотеки средствами ОС | [#224](https://github.com/kniazkov/g0at/issues/224) |
| 66 | Машиночитаемая инспекция и сведения об авторе | [#223](https://github.com/kniazkov/g0at/issues/223), [#221](https://github.com/kniazkov/g0at/issues/221) |
| 67 | Согласованный набор полей | [#221](https://github.com/kniazkov/g0at/issues/221) |
| 68 | Информационные поля исключаются из основного хеша | [#221](https://github.com/kniazkov/g0at/issues/221) |
| 69 | Конфликт информации запрещает объединение | [#222](https://github.com/kniazkov/g0at/issues/222) |
| 70 | Необязательность не допускает неверного типа | [#221](https://github.com/kniazkov/g0at/issues/221) |
| 71 | Информационные строки поддерживают Unicode | [#221](https://github.com/kniazkov/g0at/issues/221) |
| 72 | Сетевые проверки не выполняются | [#221](https://github.com/kniazkov/g0at/issues/221) |
| 73 | Содержимое строк сохраняется точно | [#221](https://github.com/kniazkov/g0at/issues/221) |
| 74 | Запись автора требует непустого имени | [#221](https://github.com/kniazkov/g0at/issues/221) |
| 75 | Лицензия — информационная строка | [#221](https://github.com/kniazkov/g0at/issues/221) |
| 76 | Return вне функции вызывает исключение | [#230](https://github.com/kniazkov/g0at/issues/230) |
| 77 | Контекст инициализации распространяется на вызовы | [#230](https://github.com/kniazkov/g0at/issues/230) |
| 78 | Главный файл при прямом запуске выполняет обычную программу | [#230](https://github.com/kniazkov/g0at/issues/230) |
| 79 | Штатный exit не выполняет пользовательскую очистку | [#230](https://github.com/kniazkov/g0at/issues/230) |
| 80 | Аргумент проверяется до завершения | [#230](https://github.com/kniazkov/g0at/issues/230) |
| 81 | Единый диапазон 0–255 без встроенных констант | [#230](https://github.com/kniazkov/g0at/issues/230) |
| 82 | Буферы стандартного вывода сбрасываются | [#230](https://github.com/kniazkov/g0at/issues/230) |
| 83 | Временные нативные библиотеки очищаются | [#230](https://github.com/kniazkov/g0at/issues/230) |
| 84 | Ошибка сброса вывода не заменяет выбранный код | [#230](https://github.com/kniazkov/g0at/issues/230) |
| 85 | Штатный exit из библиотеки завершает весь процесс | [#230](https://github.com/kniazkov/g0at/issues/230) |
| 86 | Изменяемый экспорт не фиксирует первоначальную функцию | [#231](https://github.com/kniazkov/g0at/issues/231) |
| 87 | Константность ссылки не означает чистоту функции | [#231](https://github.com/kniazkov/g0at/issues/231) |
| 88 | Эффекты могут зависеть от переданного callback | [#231](https://github.com/kniazkov/g0at/issues/231) |
| 89 | Результат может быть тем же объектом, что аргумент | [#231](https://github.com/kniazkov/g0at/issues/231) |
| 90 | Новый внешний объект может разделять вложенные значения | [#231](https://github.com/kniazkov/g0at/issues/231) |
| 91 | Перечисляются конкретные готовые специализации | [#232](https://github.com/kniazkov/g0at/issues/232) |
| 92 | Условия могут быть строже типов аргументов | [#232](https://github.com/kniazkov/g0at/issues/232) |
| 93 | Неоднозначный выбор — ошибка метаданных | [#232](https://github.com/kniazkov/g0at/issues/232) |
| 94 | Проверки декларативны и не имеют побочных эффектов | [#232](https://github.com/kniazkov/g0at/issues/232) |
| 95 | Непредставимые условия не допускают небезопасную специализацию | [#232](https://github.com/kniazkov/g0at/issues/232) |
| 96 | Неприменимость специализации относится к одному вызову | [#232](https://github.com/kniazkov/g0at/issues/232) |
| 97 | Общие проверки вызова предшествуют нативному выбору | [#232](https://github.com/kniazkov/g0at/issues/232) |
| 98 | Копирование метода не переносит нативную привязку автоматически | [#232](https://github.com/kniazkov/g0at/issues/232) |
| 99 | Обнаруженное нарушение ABI не допускает повторного вызова | [#232](https://github.com/kniazkov/g0at/issues/232) |
| 100 | Required допускает VM для вызовов без специализации | [#232](https://github.com/kniazkov/g0at/issues/232) |
| 101 | Требования отражают фактическую нативную сборку | [#211](https://github.com/kniazkov/g0at/issues/211) |
| 102 | Плоская структура нативных файлов | [#183](https://github.com/kniazkov/g0at/issues/183) |
| 103 | Неописанные файлы запрещены | [#182](https://github.com/kniazkov/g0at/issues/182) |
| 104 | Символические ссылки запрещены | [#182](https://github.com/kniazkov/g0at/issues/182) |
| 105 | Единые правила путей внутри ZIP | [#182](https://github.com/kniazkov/g0at/issues/182) |
| 106 | Только хранение без сжатия и Deflate | [#182](https://github.com/kniazkov/g0at/issues/182) |
| 107 | ZIP64 пока не поддерживается | [#182](https://github.com/kniazkov/g0at/issues/182) |
| 108 | Детерминированная упаковка | [#183](https://github.com/kniazkov/g0at/issues/183) |
| 109 | Проверка размеров до и во время распаковки | [#182](https://github.com/kniazkov/g0at/issues/182) |
| 110 | CRC32 проверяется дополнительно к SHA-256 | [#182](https://github.com/kniazkov/g0at/issues/182) |
| 111 | Целочисленные поля читаются точно | [#178](https://github.com/kniazkov/g0at/issues/178) |
| 112 | Строгий UTF-8 без BOM | [#178](https://github.com/kniazkov/g0at/issues/178) |
| 113 | Unicode не нормализуется автоматически | [#178](https://github.com/kniazkov/g0at/issues/178) |
| 114 | Дубликаты ключей проверяются после декодирования | [#178](https://github.com/kniazkov/g0at/issues/178) |
| 115 | Порядок ключей при канонизации | [#180](https://github.com/kniazkov/g0at/issues/180) |
| 116 | Единственное каноническое написание строк | [#180](https://github.com/kniazkov/g0at/issues/180) |
| 117 | Оформление JSON отделено от канонизации | [#180](https://github.com/kniazkov/g0at/issues/180) |
| 118 | Части основного хэша имеют однозначные границы | [#181](https://github.com/kniazkov/g0at/issues/181) |
| 119 | Нативный хэш покрывает весь файл библиотеки | [#208](https://github.com/kniazkov/g0at/issues/208) |
| 120 | Совпадение хэша не заменяет проверку байткода | [#184](https://github.com/kniazkov/g0at/issues/184) |
| 121 | Архитектурно независимый байткод | [#184](https://github.com/kniazkov/g0at/issues/184) |
| 122 | Метаданные сверяются с байткодом | [#185](https://github.com/kniazkov/g0at/issues/185) |
| 123 | Загрузка нативной библиотеки требует применимого вызова | [#210](https://github.com/kniazkov/g0at/issues/210) |
| 124 | Нативные описания согласованы даже при native off | [#233](https://github.com/kniazkov/g0at/issues/233) |
| 125 | Общий код проверки и сборка через CMake | [#185](https://github.com/kniazkov/g0at/issues/185) |

## Дополнительные решения

- [ ] 126–131: граф зависимостей и диагностика: [#189](https://github.com/kniazkov/g0at/issues/189), [#190](https://github.com/kniazkov/g0at/issues/190)
- [ ] 132: хранение модулей до завершения программы: [#192](https://github.com/kniazkov/g0at/issues/192)
- [ ] 133–140: потоки и сериализация инициализации: [#216](https://github.com/kniazkov/g0at/issues/216), [#217](https://github.com/kniazkov/g0at/issues/217)
- [ ] 143: главный файл выполняется без блокировки и ограничений инициализатора; динамический контекст include/import: [#217](https://github.com/kniazkov/g0at/issues/217)
- [ ] 141–142: таймаут и идентичность исключения: [#239](https://github.com/kniazkov/g0at/issues/239), [#197](https://github.com/kniazkov/g0at/issues/197)

- [ ] 144: другие потоки используют готовые объявления главного файла без ожидания его завершения; неготовые объявления вызывают исключение: [#217](https://github.com/kniazkov/g0at/issues/217)

- [ ] 145: required при запуске требует совместимую специализацию главного модуля; зависимости могут исполняться на VM; загрузка остаётся ленивой: [#210](https://github.com/kniazkov/g0at/issues/210), [#232](https://github.com/kniazkov/g0at/issues/232)

- [ ] 146: относительный include разрешается от настоящего исходного файла после разрешения символических ссылок: [#186](https://github.com/kniazkov/g0at/issues/186), [#194](https://github.com/kniazkov/g0at/issues/194)

## Общая приёмка

- [ ] Многофайловые AC-01–AC-28: [#218](https://github.com/kniazkov/g0at/issues/218)
- [ ] Сквозная проверка CI, VM/native и платформ: [#219](https://github.com/kniazkov/g0at/issues/219)
- [ ] Актуализация обеих книг и прежней модели доступа: [#220](https://github.com/kniazkov/g0at/issues/220)

Каждый PR реализации должен ссылаться на задачу и соответствующие строки матрицы. Изменение контракта требует синхронного изменения RU/EN документации и этого плана.
