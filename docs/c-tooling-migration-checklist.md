# Перевод Python-инфраструктуры на Си

Общая задача: [#318](https://github.com/kniazkov/g0at/issues/318).

План составлен 27 октября 2026 по дереву `60d1eb30b2c5cec6bfbf774c9ea518e74c4c160c`. Учтены 89 Python-файлов и встроенный Python в `scripts/check_graph.sh`. Shell/cmd и CMake сохраняются; меняются только необходимые вызовы, пути и интеграция согласованной структуры.

## Правила выполнения

1. Выполнять задачи строго в порядке ниже, по одной. Каждый пункт — отдельный проверяемый шаг/PR.
2. После каждого шага дождаться полного применимого CI для текущего коммита. Красный CI сначала разобрать и исправить; не продолжать миграцию поверх ошибки.
3. Перенос сценариев подтверждается таблицей старых проверок и новых CTest ID, сравнением Python/C на одинаковых данных и негативными проверками самого теста.
4. Python-версию удалять после сверки. Helpers, нужные оставшимся Python-потребителям, временно сохранять с явным списком потребителей; окончательная очистка — #394.
5. Включать отметку выполненной реализации сразу в тот же PR. Ссылки на подтверждённый зелёный CI добавлять в описание или комментарий PR, без отдельного push ради чеклиста. Сливать PR только после зелёного CI его окончательного коммита.
6. Не менять формат/семантику языка ради миграции. #316 отвечает за форматы .gbin, #317 — публикацию готовых утилит в target. Согласовывать пути с ними, не дублировать реализацию.
7. При появлении новых Python-файлов включать их в реестр и отдельную задачу до финального удаления зависимости.

## Принятая структура

| Каталог | Назначение |
|---|---|
| lib/ | Общая статическая C-библиотека; тематические подпапки, заголовки рядом с реализацией; нет зависимостей от VM/core |
| src/ | Интерпретатор, компилятор и утилиты Goat |
| test/src/ | Исходники тестов и тестовых хостов |
| test/data/ | Только читаемые входы, программы Goat, эталоны и frozen samples |
| test/support/ | Общий тестовый фреймворк, независимые эталоны и fixtures |
| tools/ | Нетестовые C-инструменты, каждый в отдельной подпапке |
| scripts/ | Сохраняемые shell/cmd и переходные Python-файлы до удаления |
| build/ | Результаты сборки и уникальные рабочие каталоги сценариев |

Корневой CMakeLists.txt подключает отдельные каталоги. CTest — общая точка запуска; самостоятельный сценарий имеет отдельный стабильный ID и выбирается аргументом runner. Сценарии имеют общий timeout, освобождают процессы/ресурсы после ошибки; диагностика сохраняется при провале, успешные каталоги удаляются кроме --keep-work. Независимые тесты могут идти параллельно; performance запускается без конкурирующих тестов. Независимые эталоны не вызывают проверяемые production-функции для вычисления ожидаемого ответа.

## Порядок задач

- [x] 01. [#319 — Ввести корневой CMakeLists.txt](https://github.com/kniazkov/g0at/issues/319) — [PR #396](https://github.com/kniazkov/g0at/pull/396), зелёный CI: [сборка и тесты](https://github.com/kniazkov/g0at/actions/runs/40448768780), [векторы формата](https://github.com/kniazkov/g0at/actions/runs/40448768760).
- [x] 02. [#320 — Перенести исходники тестов в test/src](https://github.com/kniazkov/g0at/issues/320)
- [x] 03. [#321 — Разделить тестовые данные и код в test](https://github.com/kniazkov/g0at/issues/321)
- [x] 04. [#322 — Выделить общую статическую библиотеку из src/lib](https://github.com/kniazkov/g0at/issues/322)
- [x] 05. [#323 — Переименовать Unicode-таблицу в unicode_data.h](https://github.com/kniazkov/g0at/issues/323)
- [x] 06. [#324 — Подключить существующие наборы к CTest](https://github.com/kniazkov/g0at/issues/324)
- [x] 07. [#325 — Расширить общий запуск процессов: env и stdin](https://github.com/kniazkov/g0at/issues/325)
- [x] 08. [#326 — Добавить потоковый обмен и большие результаты дочерних процессов](https://github.com/kniazkov/g0at/issues/326)
- [x] 09. [#327 — Создать контекст сценария и управление рабочими каталогами](https://github.com/kniazkov/g0at/issues/327)
- [x] 10. [#328 — Добавить во фреймворк проверки, диагностику и дедлайны](https://github.com/kniazkov/g0at/issues/328)
- [x] 11. [#329 — Ввести регистрацию отдельных сценариев и ограничения параллелизма](https://github.com/kniazkov/g0at/issues/329)
- [x] 12. [#330 — Зафиксировать Python-выборки как тестовые входные данные](https://github.com/kniazkov/g0at/issues/330)
- [x] 13. [#331 — Перенести описание схем из schema.py в JSON](https://github.com/kniazkov/g0at/issues/331)
- [x] 14. [#332 — Перенести генератор схем на Си](https://github.com/kniazkov/g0at/issues/332)
- [x] 15. [#333 — Перенести Unicode-генератор на Си](https://github.com/kniazkov/g0at/issues/333)
- [x] 16. [#334 — Создать независимый точный эталон арифметики](https://github.com/kniazkov/g0at/issues/334)
- [x] 17. [#335 — Перенести JSON-часть эталона формата на Си](https://github.com/kniazkov/g0at/issues/335)
- [ ] 18. [#336 — Перенести двоичную часть эталона и конструкторы fixtures](https://github.com/kniazkov/g0at/issues/336)
- [ ] 19. [#337 — Перенести общие helpers выполнения и отчётов](https://github.com/kniazkov/g0at/issues/337)
- [ ] 20. [#338 — Перенести независимое наблюдение за процессами](https://github.com/kniazkov/g0at/issues/338)
- [ ] 21. [#339 — Перенести на Си: Циклы и индексирование](https://github.com/kniazkov/g0at/issues/339)
- [ ] 22. [#340 — Перенести на Си: Символы, Unicode и интерактивный ввод](https://github.com/kniazkov/g0at/issues/340)
- [ ] 23. [#341 — Перенести на Си: Standalone bytecode v1](https://github.com/kniazkov/g0at/issues/341)
- [ ] 24. [#342 — Перенести на Си: JSON метаданных](https://github.com/kniazkov/g0at/issues/342)
- [ ] 25. [#343 — Перенести на Си: Канонизация и хеши](https://github.com/kniazkov/g0at/issues/343)
- [ ] 26. [#344 — Перенести на Си: ZIP](https://github.com/kniazkov/g0at/issues/344)
- [ ] 27. [#345 — Перенести на Си: Контейнер модулей](https://github.com/kniazkov/g0at/issues/345)
- [ ] 28. [#346 — Перенести на Си: Байткод модулей](https://github.com/kniazkov/g0at/issues/346)
- [ ] 29. [#347 — Перенести на Си: ABI и выбор native-варианта](https://github.com/kniazkov/g0at/issues/347)
- [ ] 30. [#348 — Перенести на Си: Исходники и список файлов](https://github.com/kniazkov/g0at/issues/348)
- [ ] 31. [#349 — Перенести на Си: Импорты и source include](https://github.com/kniazkov/g0at/issues/349)
- [ ] 32. [#350 — Перенести на Си: Поиск модулей](https://github.com/kniazkov/g0at/issues/350)
- [ ] 33. [#351 — Перенести на Си: Зависимости модулей](https://github.com/kniazkov/g0at/issues/351)
- [ ] 34. [#352 — Перенести на Си: Граф модулей](https://github.com/kniazkov/g0at/issues/352)
- [ ] 35. [#353 — Перенести на Си: Snapshot модулей](https://github.com/kniazkov/g0at/issues/353)
- [ ] 36. [#354 — Перенести на Си: Identity и сериализация](https://github.com/kniazkov/g0at/issues/354)
- [ ] 37. [#355 — Перенести на Си: Инициализация и её ошибки](https://github.com/kniazkov/g0at/issues/355)
- [ ] 38. [#356 — Перенести на Си: Контракт инициализации и исключения](https://github.com/kniazkov/g0at/issues/356)
- [ ] 39. [#357 — Перенести на Си: Неполные области и ошибочные прототипы](https://github.com/kniazkov/g0at/issues/357)
- [ ] 40. [#358 — Перенести на Си: Базовый доступ к модулю](https://github.com/kniazkov/g0at/issues/358)
- [ ] 41. [#359 — Перенести на Си: Общие сценарии доступа и multifile](https://github.com/kniazkov/g0at/issues/359)
- [ ] 42. [#360 — Перенести на Си: Контекстные константы и provenance](https://github.com/kniazkov/g0at/issues/360)
- [ ] 43. [#361 — Перенести на Си: Присваивания и доступ](https://github.com/kniazkov/g0at/issues/361)
- [ ] 44. [#362 — Перенести на Си: Выполнение импортов и интерфейсы](https://github.com/kniazkov/g0at/issues/362)
- [ ] 45. [#363 — Перенести на Си: Диагностика исходников](https://github.com/kniazkov/g0at/issues/363)
- [ ] 46. [#364 — Перенести на Си: Информационные поля](https://github.com/kniazkov/g0at/issues/364)
- [ ] 47. [#365 — Перенести на Си: Dispatch и согласованность](https://github.com/kniazkov/g0at/issues/365)
- [ ] 48. [#366 — Перенести на Си: Валидация модулей](https://github.com/kniazkov/g0at/issues/366)
- [ ] 49. [#367 — Перенести на Си: Запись модулей](https://github.com/kniazkov/g0at/issues/367)
- [ ] 50. [#368 — Перенести на Си: Сборка и библиотека модулей](https://github.com/kniazkov/g0at/issues/368)
- [ ] 51. [#369 — Перенести на Си: Загрузка модулей](https://github.com/kniazkov/g0at/issues/369)
- [ ] 52. [#370 — Перенести на Си: Память и fault injection](https://github.com/kniazkov/g0at/issues/370)
- [ ] 53. [#371 — Перенести на Си: CLI модулей](https://github.com/kniazkov/g0at/issues/371)
- [ ] 54. [#372 — Перенести на Си: Инспектор](https://github.com/kniazkov/g0at/issues/372)
- [ ] 55. [#373 — Перенести на Си: Probe инспектора](https://github.com/kniazkov/g0at/issues/373)
- [ ] 56. [#374 — Перенести на Си: Merge модулей](https://github.com/kniazkov/g0at/issues/374)
- [ ] 57. [#375 — Перенести на Си: Дочерние процессы](https://github.com/kniazkov/g0at/issues/375)
- [ ] 58. [#376 — Перенести на Си: Служба очистки](https://github.com/kniazkov/g0at/issues/376)
- [ ] 59. [#377 — Перенести на Си: Завершение процессов и модулей](https://github.com/kniazkov/g0at/issues/377)
- [ ] 60. [#378 — Перенести на Си: Потоки модулей](https://github.com/kniazkov/g0at/issues/378)
- [ ] 61. [#379 — Перенести на Си: Native toolchains и требования](https://github.com/kniazkov/g0at/issues/379)
- [ ] 62. [#380 — Перенести на Си: Native artifacts и integrity](https://github.com/kniazkov/g0at/issues/380)
- [ ] 63. [#381 — Перенести на Си: Очистка native](https://github.com/kniazkov/g0at/issues/381)
- [ ] 64. [#382 — Перенести на Си: Native exit и значения](https://github.com/kniazkov/g0at/issues/382)
- [ ] 65. [#383 — Перенести на Си: Native runner и его самопроверки](https://github.com/kniazkov/g0at/issues/383)
- [ ] 66. [#384 — Перенести на Си: Native access и libm](https://github.com/kniazkov/g0at/issues/384)
- [ ] 67. [#385 — Перенести на Си: Native strings](https://github.com/kniazkov/g0at/issues/385)
- [ ] 68. [#386 — Перенести на Си: Насыщающая арифметика](https://github.com/kniazkov/g0at/issues/386)
- [ ] 69. [#387 — Перенести на Си: Производительность native](https://github.com/kniazkov/g0at/issues/387)
- [ ] 70. [#388 — Перенести на Си: Автономная доставка](https://github.com/kniazkov/g0at/issues/388)
- [ ] 71. [#389 — Перенести на Си: Минимальные runtime-требования](https://github.com/kniazkov/g0at/issues/389)
- [ ] 72. [#390 — Перенести на Си: Доставка glibc/musl](https://github.com/kniazkov/g0at/issues/390)
- [ ] 73. [#391 — Перенести на Си: Проверка пакетов](https://github.com/kniazkov/g0at/issues/391)
- [ ] 74. [#392 — Перенести на Си: Проверка книги и ссылок](https://github.com/kniazkov/g0at/issues/392)
- [ ] 75. [#393 — Перенести на Си: Встроенная Python-проверка SVG](https://github.com/kniazkov/g0at/issues/393)
- [ ] 76. [#394 — Удалить оставшиеся Python helpers и переходные адаптеры](https://github.com/kniazkov/g0at/issues/394)
- [ ] 77. [#395 — Доказать работу полного цикла без Python и завершить документацию](https://github.com/kniazkov/g0at/issues/395)

## Реестр исходных Python-файлов

Пути относятся к срезу выше; после переезда сохранять связь с новыми путями. Этапы могут оставлять Python helpers до последнего потребителя.

| Исходный файл | Задача переноса |
|---|---|
| `docs/module-format-v1/reference.py` | [#335](https://github.com/kniazkov/g0at/issues/335), [#336](https://github.com/kniazkov/g0at/issues/336) |
| `docs/module-format-v1/schema.py` | [#331](https://github.com/kniazkov/g0at/issues/331) |
| `docs/module-format-v1/test_reference.py` | [#335](https://github.com/kniazkov/g0at/issues/335), [#336](https://github.com/kniazkov/g0at/issues/336) |
| `scripts/check_bytecode_v1.py` | [#341](https://github.com/kniazkov/g0at/issues/341) |
| `scripts/check_characters.py` | [#340](https://github.com/kniazkov/g0at/issues/340) |
| `scripts/check_child_process.py` | [#375](https://github.com/kniazkov/g0at/issues/375) |
| `scripts/check_cleanup_service.py` | [#376](https://github.com/kniazkov/g0at/issues/376) |
| `scripts/check_for.py` | [#339](https://github.com/kniazkov/g0at/issues/339) |
| `scripts/check_index.py` | [#339](https://github.com/kniazkov/g0at/issues/339) |
| `scripts/check_inspect_probe.py` | [#373](https://github.com/kniazkov/g0at/issues/373) |
| `scripts/check_interactive.py` | [#340](https://github.com/kniazkov/g0at/issues/340) |
| `scripts/check_module_abi.py` | [#347](https://github.com/kniazkov/g0at/issues/347) |
| `scripts/check_module_assignment_access.py` | [#361](https://github.com/kniazkov/g0at/issues/361) |
| `scripts/check_module_book.py` | [#392](https://github.com/kniazkov/g0at/issues/392) |
| `scripts/check_module_build.py` | [#368](https://github.com/kniazkov/g0at/issues/368) |
| `scripts/check_module_bytecode.py` | [#346](https://github.com/kniazkov/g0at/issues/346) |
| `scripts/check_module_canonical.py` | [#343](https://github.com/kniazkov/g0at/issues/343) |
| `scripts/check_module_cli.py` | [#371](https://github.com/kniazkov/g0at/issues/371) |
| `scripts/check_module_consistency.py` | [#365](https://github.com/kniazkov/g0at/issues/365) |
| `scripts/check_module_container.py` | [#345](https://github.com/kniazkov/g0at/issues/345) |
| `scripts/check_module_context_constants.py` | [#360](https://github.com/kniazkov/g0at/issues/360) |
| `scripts/check_module_delivery.py` | [#388](https://github.com/kniazkov/g0at/issues/388) |
| `scripts/check_module_delivery_requirements.py` | [#389](https://github.com/kniazkov/g0at/issues/389) |
| `scripts/check_module_dependencies.py` | [#351](https://github.com/kniazkov/g0at/issues/351) |
| `scripts/check_module_dispatch.py` | [#365](https://github.com/kniazkov/g0at/issues/365) |
| `scripts/check_module_exception_propagation.py` | [#356](https://github.com/kniazkov/g0at/issues/356) |
| `scripts/check_module_exit.py` | [#377](https://github.com/kniazkov/g0at/issues/377) |
| `scripts/check_module_failed_prototypes.py` | [#357](https://github.com/kniazkov/g0at/issues/357) |
| `scripts/check_module_files.py` | [#348](https://github.com/kniazkov/g0at/issues/348) |
| `scripts/check_module_function_provenance.py` | [#360](https://github.com/kniazkov/g0at/issues/360) |
| `scripts/check_module_graph.py` | [#352](https://github.com/kniazkov/g0at/issues/352) |
| `scripts/check_module_hash.py` | [#343](https://github.com/kniazkov/g0at/issues/343) |
| `scripts/check_module_identity.py` | [#354](https://github.com/kniazkov/g0at/issues/354) |
| `scripts/check_module_import_execution.py` | [#362](https://github.com/kniazkov/g0at/issues/362) |
| `scripts/check_module_imports.py` | [#349](https://github.com/kniazkov/g0at/issues/349) |
| `scripts/check_module_incomplete_scopes.py` | [#357](https://github.com/kniazkov/g0at/issues/357) |
| `scripts/check_module_information.py` | [#364](https://github.com/kniazkov/g0at/issues/364) |
| `scripts/check_module_information_merge.py` | [#364](https://github.com/kniazkov/g0at/issues/364) |
| `scripts/check_module_initialization.py` | [#355](https://github.com/kniazkov/g0at/issues/355) |
| `scripts/check_module_initialization_contract.py` | [#356](https://github.com/kniazkov/g0at/issues/356) |
| `scripts/check_module_initialization_errors.py` | [#355](https://github.com/kniazkov/g0at/issues/355) |
| `scripts/check_module_inspect.py` | [#372](https://github.com/kniazkov/g0at/issues/372) |
| `scripts/check_module_interface.py` | [#362](https://github.com/kniazkov/g0at/issues/362) |
| `scripts/check_module_json.py` | [#342](https://github.com/kniazkov/g0at/issues/342) |
| `scripts/check_module_library.py` | [#368](https://github.com/kniazkov/g0at/issues/368) |
| `scripts/check_module_loading.py` | [#369](https://github.com/kniazkov/g0at/issues/369) |
| `scripts/check_module_memory.py` | [#370](https://github.com/kniazkov/g0at/issues/370) |
| `scripts/check_module_merge.py` | [#374](https://github.com/kniazkov/g0at/issues/374) |
| `scripts/check_module_multifile_access.py` | [#359](https://github.com/kniazkov/g0at/issues/359) |
| `scripts/check_module_musl.py` | [#390](https://github.com/kniazkov/g0at/issues/390) |
| `scripts/check_module_native.py` | [#347](https://github.com/kniazkov/g0at/issues/347) |
| `scripts/check_module_native_values.py` | [#382](https://github.com/kniazkov/g0at/issues/382) |
| `scripts/check_module_search.py` | [#350](https://github.com/kniazkov/g0at/issues/350) |
| `scripts/check_module_serialization.py` | [#354](https://github.com/kniazkov/g0at/issues/354) |
| `scripts/check_module_snapshot.py` | [#353](https://github.com/kniazkov/g0at/issues/353) |
| `scripts/check_module_source_diagnostics.py` | [#363](https://github.com/kniazkov/g0at/issues/363) |
| `scripts/check_module_sources.py` | [#348](https://github.com/kniazkov/g0at/issues/348) |
| `scripts/check_module_threads.py` | [#378](https://github.com/kniazkov/g0at/issues/378) |
| `scripts/check_module_validation.py` | [#366](https://github.com/kniazkov/g0at/issues/366) |
| `scripts/check_module_writer.py` | [#367](https://github.com/kniazkov/g0at/issues/367) |
| `scripts/check_native.py` | [#383](https://github.com/kniazkov/g0at/issues/383) |
| `scripts/check_native_artifact.py` | [#380](https://github.com/kniazkov/g0at/issues/380) |
| `scripts/check_native_cleanup.py` | [#381](https://github.com/kniazkov/g0at/issues/381) |
| `scripts/check_native_exit.py` | [#382](https://github.com/kniazkov/g0at/issues/382) |
| `scripts/check_native_integrity.py` | [#380](https://github.com/kniazkov/g0at/issues/380) |
| `scripts/check_native_performance.py` | [#387](https://github.com/kniazkov/g0at/issues/387) |
| `scripts/check_native_requirements.py` | [#379](https://github.com/kniazkov/g0at/issues/379) |
| `scripts/check_native_toolchains.py` | [#379](https://github.com/kniazkov/g0at/issues/379) |
| `scripts/check_package.py` | [#391](https://github.com/kniazkov/g0at/issues/391) |
| `scripts/check_process_exit.py` | [#377](https://github.com/kniazkov/g0at/issues/377) |
| `scripts/check_source_include.py` | [#349](https://github.com/kniazkov/g0at/issues/349) |
| `scripts/check_unicode.py` | [#340](https://github.com/kniazkov/g0at/issues/340) |
| `scripts/check_zip.py` | [#344](https://github.com/kniazkov/g0at/issues/344) |
| `scripts/generate_module_json_schema.py` | [#332](https://github.com/kniazkov/g0at/issues/332) |
| `scripts/generate_unicode.py` | [#323](https://github.com/kniazkov/g0at/issues/323), [#333](https://github.com/kniazkov/g0at/issues/333) |
| `scripts/inspect_report_test_support.py` | [#337](https://github.com/kniazkov/g0at/issues/337) |
| `scripts/module_access_basic_cases.py` | [#359](https://github.com/kniazkov/g0at/issues/359) |
| `scripts/module_access_boundary_cases.py` | [#359](https://github.com/kniazkov/g0at/issues/359) |
| `scripts/module_access_copy_cases.py` | [#359](https://github.com/kniazkov/g0at/issues/359) |
| `scripts/module_execution_support.py` | [#337](https://github.com/kniazkov/g0at/issues/337) |
| `scripts/module_interface_fixtures.py` | [#336](https://github.com/kniazkov/g0at/issues/336), [#394](https://github.com/kniazkov/g0at/issues/394) |
| `scripts/process_test_support.py` | [#338](https://github.com/kniazkov/g0at/issues/338) |
| `scripts/test_module_access.py` | [#358](https://github.com/kniazkov/g0at/issues/358) |
| `test/src/native/checks/access.py` | [#384](https://github.com/kniazkov/g0at/issues/384) |
| `test/src/native/checks/libm.py` | [#384](https://github.com/kniazkov/g0at/issues/384) |
| `test/src/native/checks/runner.py` | [#383](https://github.com/kniazkov/g0at/issues/383) |
| `test/src/native/checks/runtime_strings.py` | [#385](https://github.com/kniazkov/g0at/issues/385) |
| `test/src/native/checks/saturation.py` | [#386](https://github.com/kniazkov/g0at/issues/386) |
| `test/src/native/checks/string_method_errors.py` | [#385](https://github.com/kniazkov/g0at/issues/385) |

Встроенная Python-проверка SVG: [#393](https://github.com/kniazkov/g0at/issues/393). Финальная проверка охватывает также импорты, манифесты, sys.executable, heredoc и команды CI.

### Временные инструменты фиксации выборок (#330)

К исходному реестру 89 файлов добавлены четыре временных Python-инструмента:
`test/support/freeze_python_samples.py`, `test/support/python_samples_binary.py`,
`test/support/python_samples_structured.py`, `test/support/verify_original_python_samples.py`.
Они воспроизводят/проверяют исходные выборки; обычные потребители только читают
`test/data/python-samples/`. Удаление этих инструментов входит в финальный
Python-free gate [#395](https://github.com/kniazkov/g0at/issues/395), не меняя порядка задач.
Происхождение, форматы, соответствие сценариев и команды проверки описаны в
[`test/data/python-samples/README.md`](../test/data/python-samples/README.md).
