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

Дополнительная тестовая зависимость #382: в `test/src/component/module_native_values_testing.c` сохраняются реальные ABI assertions при `NDEBUG` через локальный guard перед `<assert.h>`; original/C parity использует один и тот же host в Debug/Release, production не меняется.

Совместимый слой #383 `scripts/check_native.py` удалён в #385 после полного original/C parity последнего потребителя `string_method_errors.py`. Manifest/main, самопроверки и все check-program adapters работают на Си после #386. Логический manifest ID `checks/saturation.py` сохранён, но больше не требует Python runtime.

## Порядок задач

- [x] 01. [#319 — Ввести корневой CMakeLists.txt](https://github.com/kniazkov/g0at/issues/319) — [PR #396](https://github.com/kniazkov/g0at/pull/396), зелёный CI: [сборка и тесты](https://github.com/kniazkov/g0at/actions/runs/40448768780), [векторы формата](https://github.com/kniazkov/g0at/actions/runs/40448768760).
- [x] 02. [#320 — Перенести исходники тестов в test/src](https://github.com/kniazkov/g0at/issues/320) — [PR #397](https://github.com/kniazkov/g0at/pull/397), финальный `beb08cf4`: [Build](https://github.com/kniazkov/g0at/actions/runs/40456506882), [vectors](https://github.com/kniazkov/g0at/actions/runs/40456506776).
- [x] 03. [#321 — Разделить тестовые данные и код в test](https://github.com/kniazkov/g0at/issues/321) — [PR #398](https://github.com/kniazkov/g0at/pull/398), финальный `c010262a`: [Build](https://github.com/kniazkov/g0at/actions/runs/40465829825), [vectors](https://github.com/kniazkov/g0at/actions/runs/40465829824).
- [x] 04. [#322 — Выделить общую статическую библиотеку из src/lib](https://github.com/kniazkov/g0at/issues/322) — [PR #399](https://github.com/kniazkov/g0at/pull/399), финальный `95dc3147`: [Build](https://github.com/kniazkov/g0at/actions/runs/40477304539), [vectors](https://github.com/kniazkov/g0at/actions/runs/40477304477).
- [x] 05. [#323 — Переименовать Unicode-таблицу в unicode_data.h](https://github.com/kniazkov/g0at/issues/323) — [PR #400](https://github.com/kniazkov/g0at/pull/400), финальный `595d4055`: [Build](https://github.com/kniazkov/g0at/actions/runs/40486038923), [vectors](https://github.com/kniazkov/g0at/actions/runs/40486038856).
- [x] 06. [#324 — Подключить существующие наборы к CTest](https://github.com/kniazkov/g0at/issues/324) — [PR #401](https://github.com/kniazkov/g0at/pull/401), финальный `78b7199b`: [Build](https://github.com/kniazkov/g0at/actions/runs/40494431322), [vectors](https://github.com/kniazkov/g0at/actions/runs/40494431257).
- [x] 07. [#325 — Расширить общий запуск процессов: env и stdin](https://github.com/kniazkov/g0at/issues/325) — [PR #402](https://github.com/kniazkov/g0at/pull/402), финальный `cec4a6ac`: [Build](https://github.com/kniazkov/g0at/actions/runs/40503444500), [vectors](https://github.com/kniazkov/g0at/actions/runs/40503444378).
- [x] 08. [#326 — Добавить потоковый обмен и большие результаты дочерних процессов](https://github.com/kniazkov/g0at/issues/326) — [PR #403](https://github.com/kniazkov/g0at/pull/403), финальный `8b2034cc`: [Build](https://github.com/kniazkov/g0at/actions/runs/40516368469), [vectors](https://github.com/kniazkov/g0at/actions/runs/40516368601).
- [x] 09. [#327 — Создать контекст сценария и управление рабочими каталогами](https://github.com/kniazkov/g0at/issues/327) — [PR #404](https://github.com/kniazkov/g0at/pull/404), финальный `4d3854ec`: [Build](https://github.com/kniazkov/g0at/actions/runs/40527781753), [vectors](https://github.com/kniazkov/g0at/actions/runs/40527781610).
- [x] 10. [#328 — Добавить во фреймворк проверки, диагностику и дедлайны](https://github.com/kniazkov/g0at/issues/328) — [PR #405](https://github.com/kniazkov/g0at/pull/405), финальный `ebb52584`: [Build](https://github.com/kniazkov/g0at/actions/runs/40541198360), [vectors](https://github.com/kniazkov/g0at/actions/runs/40541197829).
- [x] 11. [#329 — Ввести регистрацию отдельных сценариев и ограничения параллелизма](https://github.com/kniazkov/g0at/issues/329) — [PR #406](https://github.com/kniazkov/g0at/pull/406), финальный `bf1be0ad`: [Build](https://github.com/kniazkov/g0at/actions/runs/40553246363), [vectors](https://github.com/kniazkov/g0at/actions/runs/40553246060).
- [x] 12. [#330 — Зафиксировать Python-выборки как тестовые входные данные](https://github.com/kniazkov/g0at/issues/330) — [PR #407](https://github.com/kniazkov/g0at/pull/407), финальный `7d3ea84b`: [Build](https://github.com/kniazkov/g0at/actions/runs/40565846949), [vectors](https://github.com/kniazkov/g0at/actions/runs/40565847017).
- [x] 13. [#331 — Перенести описание схем из schema.py в JSON](https://github.com/kniazkov/g0at/issues/331) — [PR #408](https://github.com/kniazkov/g0at/pull/408), финальный `381de72e`: [Build](https://github.com/kniazkov/g0at/actions/runs/40576101843), [vectors](https://github.com/kniazkov/g0at/actions/runs/40576101733).
- [x] 14. [#332 — Перенести генератор схем на Си](https://github.com/kniazkov/g0at/issues/332) — [PR #409](https://github.com/kniazkov/g0at/pull/409), финальный `6b2738e5`: [Build](https://github.com/kniazkov/g0at/actions/runs/40586690168), [vectors](https://github.com/kniazkov/g0at/actions/runs/40586690118).
- [x] 15. [#333 — Перенести Unicode-генератор на Си](https://github.com/kniazkov/g0at/issues/333) — [PR #410](https://github.com/kniazkov/g0at/pull/410), финальный `62469018`: [Build](https://github.com/kniazkov/g0at/actions/runs/40595980054), [vectors](https://github.com/kniazkov/g0at/actions/runs/40595980032).
- [x] 16. [#334 — Создать независимый точный эталон арифметики](https://github.com/kniazkov/g0at/issues/334) — [PR #411](https://github.com/kniazkov/g0at/pull/411), финальный `9da1e82f`: [Build](https://github.com/kniazkov/g0at/actions/runs/40615354737), [vectors](https://github.com/kniazkov/g0at/actions/runs/40615354850).
- [x] 17. [#335 — Перенести JSON-часть эталона формата на Си](https://github.com/kniazkov/g0at/issues/335) — [PR #412](https://github.com/kniazkov/g0at/pull/412), финальный `126bf200`: [Build](https://github.com/kniazkov/g0at/actions/runs/40623039920), [vectors](https://github.com/kniazkov/g0at/actions/runs/40623039976).
- [x] 18. [#336 — Перенести двоичную часть эталона и конструкторы fixtures](https://github.com/kniazkov/g0at/issues/336) — [PR #413](https://github.com/kniazkov/g0at/pull/413), финальный `0746664e`: [Build](https://github.com/kniazkov/g0at/actions/runs/40630115640), [vectors](https://github.com/kniazkov/g0at/actions/runs/40630115714).
- [x] 19. [#337 — Перенести общие helpers выполнения и отчётов](https://github.com/kniazkov/g0at/issues/337) — [PR #414](https://github.com/kniazkov/g0at/pull/414), финальный `2d0651e6`: [Build](https://github.com/kniazkov/g0at/actions/runs/40637480458), [vectors](https://github.com/kniazkov/g0at/actions/runs/40637480331).
- [x] 20. [#338 — Перенести независимое наблюдение за процессами](https://github.com/kniazkov/g0at/issues/338) — [PR #415](https://github.com/kniazkov/g0at/pull/415), финальный `06748c5f`: [Build](https://github.com/kniazkov/g0at/actions/runs/40646387008), [vectors](https://github.com/kniazkov/g0at/actions/runs/40646386998).
- [x] 21. [#339 — Перенести на Си: Циклы и индексирование](https://github.com/kniazkov/g0at/issues/339) — [карта сценариев и проверка](../test/src/functional/README.md). [PR #416](https://github.com/kniazkov/g0at/pull/416), финальный `ec1d084f`: [Build](https://github.com/kniazkov/g0at/actions/runs/40654832482), [vectors](https://github.com/kniazkov/g0at/actions/runs/40654832454).
- [x] 22. [#340 — Перенести на Си: Символы, Unicode и интерактивный ввод](https://github.com/kniazkov/g0at/issues/340) — [карта сценариев и проверка](../test/src/functional/README.md). [PR #417](https://github.com/kniazkov/g0at/pull/417), финальный `42c0e2da`: [Build](https://github.com/kniazkov/g0at/actions/runs/40663963766), [vectors](https://github.com/kniazkov/g0at/actions/runs/40663963869).
- [x] 23. [#341 — Перенести на Си: Standalone bytecode v1](https://github.com/kniazkov/g0at/issues/341) — [карта сценариев и проверка](../test/src/functional/README.md). [PR #418](https://github.com/kniazkov/g0at/pull/418), финальный `2d3c2fc5`: [Build](https://github.com/kniazkov/g0at/actions/runs/40672813024), [vectors](https://github.com/kniazkov/g0at/actions/runs/40672813069).
- [x] 24. [#342 — Перенести на Си: JSON метаданных](https://github.com/kniazkov/g0at/issues/342) — [карта сценариев и проверка](../test/src/functional/README.md). [PR #419](https://github.com/kniazkov/g0at/pull/419), финальный `52ffd7b6`: [Build](https://github.com/kniazkov/g0at/actions/runs/40682622549), [vectors](https://github.com/kniazkov/g0at/actions/runs/40682622468).
- [x] 25. [#343 — Перенести на Си: Канонизация и хеши](https://github.com/kniazkov/g0at/issues/343) — [карта сценариев и проверка](../test/src/functional/README.md). [PR #420](https://github.com/kniazkov/g0at/pull/420), финальный `007c8acd`: [Build](https://github.com/kniazkov/g0at/actions/runs/40698407050), [vectors](https://github.com/kniazkov/g0at/actions/runs/40698407049).
- [x] 26. [#344 — Перенести на Си: ZIP](https://github.com/kniazkov/g0at/issues/344) — [карта сценариев и проверка](../test/src/functional/README.md). [PR #421](https://github.com/kniazkov/g0at/pull/421), финальный `95b7e1c7`: [Build](https://github.com/kniazkov/g0at/actions/runs/40712091318), [vectors](https://github.com/kniazkov/g0at/actions/runs/40712091397).
- [x] 27. [#345 — Перенести на Си: Контейнер модулей](https://github.com/kniazkov/g0at/issues/345) — [карта сценариев и проверка](../test/src/functional/README.md). [PR #422](https://github.com/kniazkov/g0at/pull/422), финальный `eb23ad8a`: [Build](https://github.com/kniazkov/g0at/actions/runs/40725960711), [vectors](https://github.com/kniazkov/g0at/actions/runs/40725960413).
- [x] 28. [#346 — Перенести на Си: Байткод модулей](https://github.com/kniazkov/g0at/issues/346) — [карта сценариев и проверка](../test/src/functional/README.md). [PR #423](https://github.com/kniazkov/g0at/pull/423), финальный `fe660ab1`: [Build, attempt 2](https://github.com/kniazkov/g0at/actions/runs/40739969024), [vectors](https://github.com/kniazkov/g0at/actions/runs/40739969133).
- [x] 29. [#347 — Перенести на Си: ABI и выбор native-варианта](https://github.com/kniazkov/g0at/issues/347) — [карта сценариев и проверка](../test/src/functional/README.md). [PR #424](https://github.com/kniazkov/g0at/pull/424), финальный `6eddd331`: [Build](https://github.com/kniazkov/g0at/actions/runs/40764196818), [vectors](https://github.com/kniazkov/g0at/actions/runs/40764196892).
- [x] 30. [#348 — Перенести на Си: Исходники и список файлов](https://github.com/kniazkov/g0at/issues/348) — [карта сценариев и проверка](../test/src/functional/README.md). [PR #425](https://github.com/kniazkov/g0at/pull/425), финальный `986b56e9`: [Build](https://github.com/kniazkov/g0at/actions/runs/40775649193), [vectors](https://github.com/kniazkov/g0at/actions/runs/40775649316).
- [x] 31. [#349 — Перенести на Си: Импорты и source include](https://github.com/kniazkov/g0at/issues/349) — [карта сценариев и проверка](../test/src/functional/README.md). [PR #426](https://github.com/kniazkov/g0at/pull/426), финальный `03d9f1fd`: [Build](https://github.com/kniazkov/g0at/actions/runs/40785904458), [vectors](https://github.com/kniazkov/g0at/actions/runs/40785904382).
- [x] 32. [#350 — Перенести на Си: Поиск модулей](https://github.com/kniazkov/g0at/issues/350) — [карта сценариев и проверка](../test/src/functional/README.md). [PR #427](https://github.com/kniazkov/g0at/pull/427), финальный `ce403935`: [Build](https://github.com/kniazkov/g0at/actions/runs/40794499119), [vectors](https://github.com/kniazkov/g0at/actions/runs/40794499016).
- [x] 33. [#351 — Перенести на Си: Зависимости модулей](https://github.com/kniazkov/g0at/issues/351) — [карта сценариев и проверка](../test/src/functional/README.md). [PR #428](https://github.com/kniazkov/g0at/pull/428), финальный `31898053`: [Build](https://github.com/kniazkov/g0at/actions/runs/40802565482), [vectors](https://github.com/kniazkov/g0at/actions/runs/40802565690).
- [x] 34. [#352 — Перенести на Си: Граф модулей](https://github.com/kniazkov/g0at/issues/352) — [карта сценариев и проверка](../test/src/functional/README.md). [PR #429](https://github.com/kniazkov/g0at/pull/429), финальный `47bfefb8`: [Build](https://github.com/kniazkov/g0at/actions/runs/40810921495), [vectors](https://github.com/kniazkov/g0at/actions/runs/40810921393).
- [x] 35. [#353 — Перенести на Си: Snapshot модулей](https://github.com/kniazkov/g0at/issues/353) — [карта сценариев и проверка](../test/src/functional/README.md). [PR #430](https://github.com/kniazkov/g0at/pull/430), финальный `32accab1`: [Build](https://github.com/kniazkov/g0at/actions/runs/40818825037), [vectors](https://github.com/kniazkov/g0at/actions/runs/40818825308).
- [x] 36. [#354 — Перенести на Си: Identity и сериализация](https://github.com/kniazkov/g0at/issues/354) — [карта сценариев и проверка](../test/src/functional/README.md). [PR #431](https://github.com/kniazkov/g0at/pull/431), финальный `e27f5ddb`: [Build](https://github.com/kniazkov/g0at/actions/runs/40826155806), [vectors](https://github.com/kniazkov/g0at/actions/runs/40826155801).
- [x] 37. [#355 — Перенести на Си: Инициализация и её ошибки](https://github.com/kniazkov/g0at/issues/355) — [карта сценариев и проверка](../test/src/functional/README.md). [PR #432](https://github.com/kniazkov/g0at/pull/432), финальный `0cd027fc`: [Build](https://github.com/kniazkov/g0at/actions/runs/40833724367), [vectors](https://github.com/kniazkov/g0at/actions/runs/40833724245).
- [x] 38. [#356 — Перенести на Си: Контракт инициализации и исключения](https://github.com/kniazkov/g0at/issues/356) — [карта сценариев и проверка](../test/src/functional/README.md). [PR #433](https://github.com/kniazkov/g0at/pull/433), финальный `3bb025d1`: [Build](https://github.com/kniazkov/g0at/actions/runs/40841799209), [vectors](https://github.com/kniazkov/g0at/actions/runs/40841799223).
- [x] 39. [#357 — Перенести на Си: Неполные области и ошибочные прототипы](https://github.com/kniazkov/g0at/issues/357) — [карта сценариев и проверка](../test/src/functional/README.md). [PR #434](https://github.com/kniazkov/g0at/pull/434), финальный `103198fd`: [Build](https://github.com/kniazkov/g0at/actions/runs/40855168966), [vectors](https://github.com/kniazkov/g0at/actions/runs/40855168968).
- [x] 40. [#358 — Перенести на Си: Базовый доступ к модулю](https://github.com/kniazkov/g0at/issues/358) — [карта сценариев и проверка](../test/src/functional/README.md). [PR #435](https://github.com/kniazkov/g0at/pull/435), финальный `a7711d34`: [Build](https://github.com/kniazkov/g0at/actions/runs/40866214368), [vectors](https://github.com/kniazkov/g0at/actions/runs/40866214429).
- [x] 41. [#359 — Перенести на Си: Общие сценарии доступа и multifile](https://github.com/kniazkov/g0at/issues/359) — [карта сценариев и проверка](../test/data/module-access/README.md). [PR #436](https://github.com/kniazkov/g0at/pull/436), финальный `c8f6324a`: [Build](https://github.com/kniazkov/g0at/actions/runs/40884601161), [vectors](https://github.com/kniazkov/g0at/actions/runs/40884600971).
- [x] 42. [#360 — Перенести на Си: Контекстные константы и provenance](https://github.com/kniazkov/g0at/issues/360) — [контекст](../test/data/module-context/README.md), [provenance](../test/data/module-provenance/README.md). [PR #437](https://github.com/kniazkov/g0at/pull/437), финальный `00e24fc3`: [Build](https://github.com/kniazkov/g0at/actions/runs/40905300797), [vectors](https://github.com/kniazkov/g0at/actions/runs/40905300783).
- [x] 43. [#361 — Перенести на Си: Присваивания и доступ](https://github.com/kniazkov/g0at/issues/361) — [карта сценариев и проверка](../test/data/module-assignment/README.md). [PR #438](https://github.com/kniazkov/g0at/pull/438), финальный `0d4ab97b`: [Build](https://github.com/kniazkov/g0at/actions/runs/40944331282), [vectors](https://github.com/kniazkov/g0at/actions/runs/40944331138).
- [x] 44. [#362 — Перенести на Си: Выполнение импортов и интерфейсы](https://github.com/kniazkov/g0at/issues/362) — [PR #439](https://github.com/kniazkov/g0at/pull/439), финальный `deb593d0`: [Build](https://github.com/kniazkov/g0at/actions/runs/41004788329), [vectors](https://github.com/kniazkov/g0at/actions/runs/41004788328).
- [x] 45. [#363 — Перенести на Си: Диагностика исходников](https://github.com/kniazkov/g0at/issues/363) — [карта сценариев и проверка](../test/data/module-source-diagnostics/README.md). [PR #440](https://github.com/kniazkov/g0at/pull/440), финальный `bdc2c543`: [Build](https://github.com/kniazkov/g0at/actions/runs/41013164016), [vectors](https://github.com/kniazkov/g0at/actions/runs/41013164064).
- [x] 46. [#364 — Перенести на Си: Информационные поля](https://github.com/kniazkov/g0at/issues/364) — [карта сценариев и проверка](../test/data/module-information/README.md); сохранены assertions в writer test-host для Release. [PR #441](https://github.com/kniazkov/g0at/pull/441), финальный `9b2fcf3e`: [Build](https://github.com/kniazkov/g0at/actions/runs/41022849893), [vectors](https://github.com/kniazkov/g0at/actions/runs/41022849918).
- [x] 47. [#365 — Перенести на Си: Dispatch и согласованность](https://github.com/kniazkov/g0at/issues/365) — [карта и проверки](../test/data/module-dispatch-consistency/README.md); assertions в трёх test-host TU сохранены для Release; полный producer — обязательный CI, локально cleanup=2. [PR #442](https://github.com/kniazkov/g0at/pull/442), финальный `50ab624e`: [Build](https://github.com/kniazkov/g0at/actions/runs/41036528092), [vectors](https://github.com/kniazkov/g0at/actions/runs/41036528054).
- [x] 48. [#366 — Перенести на Си: Валидация модулей](https://github.com/kniazkov/g0at/issues/366) — [карта и проверки](../test/data/module-validation/README.md); assertions validation test-host сохранены для Release. [PR #443](https://github.com/kniazkov/g0at/pull/443), финальный `369c858b`: [Build](https://github.com/kniazkov/g0at/actions/runs/41047862273), [vectors](https://github.com/kniazkov/g0at/actions/runs/41047862313).
- [x] 49. [#367 — Перенести на Си: Запись модулей](https://github.com/kniazkov/g0at/issues/367) — [карта и проверки](../test/data/module-writer/README.md); partial-write assertions fault-host сохранены для Release. [PR #444](https://github.com/kniazkov/g0at/pull/444), финальный `409e5e10`: [Build](https://github.com/kniazkov/g0at/actions/runs/41063017999), [vectors](https://github.com/kniazkov/g0at/actions/runs/41063017592).
- [x] 50. [#368 — Перенести на Си: Сборка и библиотека модулей](https://github.com/kniazkov/g0at/issues/368) — [карта сборки](../test/data/module-build/README.md), [карта библиотек](../test/data/module-library/README.md); assertions library test-host сохранены для Release. [PR #445](https://github.com/kniazkov/g0at/pull/445), финальный `d6858dc0`: [Build](https://github.com/kniazkov/g0at/actions/runs/41086477192), [vectors](https://github.com/kniazkov/g0at/actions/runs/41086477172).
- [x] 51. [#369 — Перенести на Си: Загрузка модулей](https://github.com/kniazkov/g0at/issues/369) — test-only зависимость: assertions loading-host для Release и owner.pid для независимого наблюдения owner. [PR #446](https://github.com/kniazkov/g0at/pull/446), финальный `f871aea7`: [Build](https://github.com/kniazkov/g0at/actions/runs/41106605022), [vectors](https://github.com/kniazkov/g0at/actions/runs/41106604526).
- [x] 52. [#370 — Перенести на Си: Память и fault injection](https://github.com/kniazkov/g0at/issues/370) — [карта и проверки](../test/data/module-memory/README.md); test-only зависимость: assertions memory-host сохранены для Release. [PR #448](https://github.com/kniazkov/g0at/pull/448), финальный `262da025`: [Build](https://github.com/kniazkov/g0at/actions/runs/41175516414), [vectors](https://github.com/kniazkov/g0at/actions/runs/41175516212).
- [x] 53. [#371 — Перенести на Си: CLI модулей](https://github.com/kniazkov/g0at/issues/371) — [карта и проверки](../test/data/module-cli/README.md); test-only зависимость: assertions compiler-wrapper для Release. [PR #449](https://github.com/kniazkov/g0at/pull/449), финальный `20e9a2c9`: [Build](https://github.com/kniazkov/g0at/actions/runs/41184775672), [vectors](https://github.com/kniazkov/g0at/actions/runs/41184775871).
- [x] 54. [#372 — Перенести на Си: Инспектор](https://github.com/kniazkov/g0at/issues/372) — [PR #450](https://github.com/kniazkov/g0at/pull/450), финальный `d88d3343`: [Build](https://github.com/kniazkov/g0at/actions/runs/41194315075), [vectors](https://github.com/kniazkov/g0at/actions/runs/41194315096).
- [x] 55. [#373 — Перенести на Си: Probe инспектора](https://github.com/kniazkov/g0at/issues/373) — [PR #451](https://github.com/kniazkov/g0at/pull/451), финальный `af2da0e2`: [Build](https://github.com/kniazkov/g0at/actions/runs/41209007038), [vectors](https://github.com/kniazkov/g0at/actions/runs/41209007037).
- [x] 56. [#374 — Перенести на Си: Merge модулей](https://github.com/kniazkov/g0at/issues/374) — [PR #452](https://github.com/kniazkov/g0at/pull/452), финальный `8a13b980`: [Build](https://github.com/kniazkov/g0at/actions/runs/41234288663), [vectors](https://github.com/kniazkov/g0at/actions/runs/41234288643).
- [x] 57. [#375 — Перенести на Си: Дочерние процессы](https://github.com/kniazkov/g0at/issues/375) — [PR #453](https://github.com/kniazkov/g0at/pull/453), финальный `9bd0f8ac`: [Build](https://github.com/kniazkov/g0at/actions/runs/41266583280), [vectors](https://github.com/kniazkov/g0at/actions/runs/41266583316).
- [x] 58. [#376 — Перенести на Си: Служба очистки](https://github.com/kniazkov/g0at/issues/376) — [PR #454](https://github.com/kniazkov/g0at/pull/454), финальный `9b0822a1`: [Build](https://github.com/kniazkov/g0at/actions/runs/41287211838), [vectors](https://github.com/kniazkov/g0at/actions/runs/41287211839).
- [x] 59. [#377 — Перенести на Си: Завершение процессов и модулей](https://github.com/kniazkov/g0at/issues/377) — test-only зависимость: assertions process-exit fixture сохранены для Release во всех трёх hosts, scoped deadline helper сохраняет per-command 60 s module-exit. [PR #455](https://github.com/kniazkov/g0at/pull/455), финальный `bad2b0d5`: [Build](https://github.com/kniazkov/g0at/actions/runs/41302839042), [vectors](https://github.com/kniazkov/g0at/actions/runs/41302839047).
- [x] 60. [#378 — Перенести на Си: Потоки модулей](https://github.com/kniazkov/g0at/issues/378) — test-only зависимость: assertions module-thread-lifetime fixture сохраняются для Release, raw-owner transport получает optional cwd и inherited environment для исходного handshake/lifetime. [PR #456](https://github.com/kniazkov/g0at/pull/456), финальный `806b532c`: [Build](https://github.com/kniazkov/g0at/actions/runs/41317710015), [vectors](https://github.com/kniazkov/g0at/actions/runs/41317710077).
- [x] 61. [#379 — Перенести на Си: Native toolchains и требования](https://github.com/kniazkov/g0at/issues/379) — [PR #457](https://github.com/kniazkov/g0at/pull/457), финальный `6bd85583`: [Build](https://github.com/kniazkov/g0at/actions/runs/41336140585), [vectors](https://github.com/kniazkov/g0at/actions/runs/41336140515).
- [x] 62. [#380 — Перенести на Си: Native artifacts и integrity](https://github.com/kniazkov/g0at/issues/380) — test-only зависимость: path/bytes assertion native-artifact fixture сохраняется в Release. [PR #458](https://github.com/kniazkov/g0at/pull/458), финальный `aa0292a5`: [Build](https://github.com/kniazkov/g0at/actions/runs/41390639650), [vectors](https://github.com/kniazkov/g0at/actions/runs/41390639719).
- [x] 63. [#381 — Перенести на Си: Очистка native](https://github.com/kniazkov/g0at/issues/381) — test-only зависимость: раздельные close-stdin/wait-stderr raw-owner операции сохраняют исходные EOF deadlines. [PR #459](https://github.com/kniazkov/g0at/pull/459), финальный `9f09fa4b`: [Build](https://github.com/kniazkov/g0at/actions/runs/41413304852), [vectors](https://github.com/kniazkov/g0at/actions/runs/41413304828).
- [x] 64. [#382 — Перенести на Си: Native exit и значения](https://github.com/kniazkov/g0at/issues/382) — [PR #460](https://github.com/kniazkov/g0at/pull/460), original/C parity: [сборка и тесты](https://github.com/kniazkov/g0at/actions/runs/41438905339), [векторы](https://github.com/kniazkov/g0at/actions/runs/41438905517).
- [x] 65. [#383 — Перенести на Си: Native runner и его самопроверки](https://github.com/kniazkov/g0at/issues/383) — [PR #461](https://github.com/kniazkov/g0at/pull/461), original/C parity: [сборка и тесты](https://github.com/kniazkov/g0at/actions/runs/41495133812), [векторы](https://github.com/kniazkov/g0at/actions/runs/41495133951); compatibility helpers последнего потребителя удалены в #385 после полного parity.
- [x] 66. [#384 — Перенести на Си: Native access и libm](https://github.com/kniazkov/g0at/issues/384) — [PR #462](https://github.com/kniazkov/g0at/pull/462), original/C parity: [сборка и тесты](https://github.com/kniazkov/g0at/actions/runs/41578881805), [векторы](https://github.com/kniazkov/g0at/actions/runs/41578881774); 64 frozen inputs и исходные deadlines сохранены, Python удалён после полного gate.
- [x] 67. [#385 — Перенести на Си: Native strings](https://github.com/kniazkov/g0at/issues/385) — [PR #463](https://github.com/kniazkov/g0at/pull/463), original/C parity: [сборка и тесты](https://github.com/kniazkov/g0at/actions/runs/41597613910), [векторы](https://github.com/kniazkov/g0at/actions/runs/41597613872); 281 runtime comparison, 13 frozen inputs, ABI/lifetime, method reports и sanitizer flags сохранены; оба Python-checker и последний compatibility helper удалены после полного gate.
- [x] 68. [#386 — Перенести на Си: Насыщающая арифметика](https://github.com/kniazkov/g0at/issues/386) — [PR #464](https://github.com/kniazkov/g0at/pull/464), original/C parity на `38a029e15a4fab21942a3782dc14ef92b3d48892`: [сборка и тесты, attempt 4, 11/11](https://github.com/kniazkov/g0at/actions/runs/41623617966/attempts/4), [векторы, 3/3](https://github.com/kniazkov/g0at/actions/runs/41623617722); 29 C-сценариев, 1196 пар и 7176 assertions на вариант, deadlines 120/30/300 с сохранены. Python и временный parity wrapper удалены только после полного gate; финальный `7380e1e`: [Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41654522348), [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41654522178), оба attempt 1. Последующая проблема ARM на merged master и её исправления PR #467 сохраняются в истории; этот результат не отменяет их.
- [x] 69. [#387 — Перенести на Си: Производительность native](https://github.com/kniazkov/g0at/issues/387) — [PR #468](https://github.com/kniazkov/g0at/pull/468), полный original/C parity на `37ce62bb55f0a9f68c35521d8349de5c9a892f5d`: [сборка и тесты, 11/11](https://github.com/kniazkov/g0at/actions/runs/41742078675), [векторы, 3/3](https://github.com/kniazkov/g0at/actions/runs/41742078588). [Карта и доказательства](../test/data/native-performance/README.md); depth=12, trees=1024, 5 чередующихся пар, медианы и strict native<VM сохранены. Test-only timed capture исключает diagnostic I/O; x87 binary64 и ожидание pinned Windows handle проверены. Python и временный wrapper удалены только после полного parity gate; финальный `2dfab4f8`: [Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41749910955), [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41749910973), оба attempt 1.
- [x] 70. [#388 — Перенести на Си: Автономная доставка](https://github.com/kniazkov/g0at/issues/388) — [PR #469](https://github.com/kniazkov/g0at/pull/469), original/C parity на `67edfd3582ca8d5f4aa65ab05fff2304e6ca7ea6`: [сборка и тесты, 11/11](https://github.com/kniazkov/g0at/actions/runs/41766231298), [векторы, 3/3](https://github.com/kniazkov/g0at/actions/runs/41766231321). Сохранены 12 C-сценариев, полный граф, native/portable проверки, Debug/Release, musl и перемещённый пакет. Python-runner и временные parity-wrapper/host удалены после полного gate; финальный head `8644d957`: [Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41772647290), [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41772647240); merged master `5602d440`: [Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41779727289), [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41779727287). Общий test-only `language_find_executable` сохраняет поиск compiler по PATH; оставшиеся compatibility helpers удалены в #394.
- [x] 71. [#389 — Перенести на Си: Минимальные runtime-требования](https://github.com/kniazkov/g0at/issues/389) — [PR #470](https://github.com/kniazkov/g0at/pull/470), original/C parity на `c285eff24cdc21be70abe383f383a550c14f7546`: [Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41788518179), [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41788518149). Все 7 платформ прошли Debug/Release parity; все 4 Linux-варианта исполнили оба артефакта обоими runner в exact-runtime контейнере. [Карта и provenance](../test/data/module-delivery-requirements/README.md). Original Python, временный wrapper и replay-audit удалены после 14/14 gate; финальный head `847a7811`: [Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41797598329), [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41797598431); merged master `eabab2df`: [Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41807274856), [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41807274882). Общие test-only process/workspace, format-oracle и executable-discovery helpers сохранены; #390 не входит в этот шаг.
- [x] 72. [#390 — Перенести на Си: Доставка glibc/musl](https://github.com/kniazkov/g0at/issues/390) — [PR #471](https://github.com/kniazkov/g0at/pull/471), original/C parity на `7c037eff4d34b03b0d490677412b4fc48e820eab`: [Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41815980098), [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41815980150). Обе архитектуры исполнили независимые original/C пары обоими runner в musl Debug/Release и glibc; исходные архивы неизменны. Python и временный parity-wrapper удалены после 14/14 gate; финальный head `508d0737`: [Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41824002764), [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41824002755); merged master `f97e0388`: [Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41834008779), [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41834008928). [Карта и provenance](../test/data/module-musl/README.md).
- [x] 73. [#391 — Перенести на Си: Проверка пакетов](https://github.com/kniazkov/g0at/issues/391) — [PR #472](https://github.com/kniazkov/g0at/pull/472), original/C parity на `47293b59f31036b771530655dca153866b49924b`: [Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41857101812), [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41857101772). Все 9 платформ прошли Debug/Release parity на одинаковых архивах. Original Python и временный wrapper удалены после 14/14 gate; финальный `6e7b947`: [Build 11/11, attempt 2](https://github.com/kniazkov/g0at/actions/runs/41869484554/attempts/2), [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41869484802). Merge `8b2550a`: [Build 11/11, attempt 2](https://github.com/kniazkov/g0at/actions/runs/41892752292/attempts/2), [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41892752274). Final attempt 1: Windows workspace cleanup error 32; master attempt 1: UCRT deliberate-crash probe reported a 1-second timeout; both passed unchanged reruns, without weakened checks. Shared test-only dependency: CMake archive extraction and module-delivery runner; no package target dependency cycle. [Fixtures and provenance](../test/data/package/README.md). #392 excluded.
- [x] 74. [#392 — Перенести на Си: Проверка книги и ссылок](https://github.com/kniazkov/g0at/issues/392) — [PR #473](https://github.com/kniazkov/g0at/pull/473), original/C parity на `17e6de35ce28531cf6b140835916147d9612748a`: [Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41915800670), [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41915800547). Все 9 платформ прошли Debug/Release parity и controls на реальных README/book/examples; сохранён отдельный musl Debug запуск. Original Python и временный wrapper удалены после 14/14 gate; финальный `4c97cea2` и merge `f6b302d1` прошли 14/14 с первой попытки; master: [Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41937212817), [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41937212719). Stable CTest ID, 15 commands, 60-second child limits, 900-second scenario budget и полные failure artifacts сохранены. Shared Python helper был оставлен до #394 и затем удалён. [Scenario map and retention](../test/data/module-book/README.md).
- [x] 75. [#393 — Перенести на Си: Встроенная Python-проверка SVG](https://github.com/kniazkov/g0at/issues/393) — original/C parity на `c6771da92d1ccfe245c041b0cd576d244b1377a2`: [Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41948450047), [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41948450046). Все 9 платформ прошли Debug/Release fixture parity и controls; реальная генерация SVG прошла в Linux GCC x86_64. Original heredoc и временный wrapper удалены после полного 14/14 gate; [PR #474](https://github.com/kniazkov/g0at/pull/474), финальный `27d0ebd7` и merged master `4a90f8c0` прошли 14/14 с первой попытки; master: [Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41963621541), [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41963621484). Stable CTest IDs, исходные Goat launches, C validator/controls и fixture bytes сохранены. [Scenario map and retention](../test/data/graph/README.md).
- [x] 76. [#394 — Удалить оставшиеся Python helpers и переходные адаптеры](https://github.com/kniazkov/g0at/issues/394) — [PR #475](https://github.com/kniazkov/g0at/pull/475), полный original/C parity на `4272323b530f919cc21e817ba684aeed3974893f`: [Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41979993274), [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41979993245). Семь неиспользуемых Python helpers и активные Python-вызовы удалены после полного gate; стабильные CTest IDs сохранены. Предыдущие проблемы маршрутизации parity, autocrlf и выбора интерпретатора описаны в PR; это не утверждение об успехе первой попытки. Финальный head `ca6472fe`: [Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41989935132), [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41989935130); merged master `3c1b75bd`: [Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41999099987), [vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41999100051). Оба final/merge gate прошли все 14 jobs с первой попытки. Четыре неактивных reproduction-инструмента удаляются отдельным шагом #395; [покрытие и команды](../test/data/format-reference/README.md).
- [x] 77. [#395 — Доказать работу полного цикла без Python и завершить документацию](https://github.com/kniazkov/g0at/issues/395) — [PR #476](https://github.com/kniazkov/g0at/pull/476), реализация финального Python-free gate и аудита завершена; CI окончательного head и post-merge master ещё ожидаются. Ссылки на подтверждённые прогоны добавляются в описание PR согласно правилу 5, без отдельного push ради чеклиста.

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
| `scripts/check_module_book.py` → `test/src/functional/module_book_scenarios.c` (Python удалён) | [#392](https://github.com/kniazkov/g0at/issues/392) |
| `scripts/check_module_build.py` | [#368](https://github.com/kniazkov/g0at/issues/368) |
| `scripts/check_module_bytecode.py` | [#346](https://github.com/kniazkov/g0at/issues/346) |
| `scripts/check_module_canonical.py` | [#343](https://github.com/kniazkov/g0at/issues/343) |
| `scripts/check_module_cli.py` | [#371](https://github.com/kniazkov/g0at/issues/371) |
| `scripts/check_module_consistency.py` | [#365](https://github.com/kniazkov/g0at/issues/365) |
| `scripts/check_module_container.py` | [#345](https://github.com/kniazkov/g0at/issues/345) |
| `scripts/check_module_context_constants.py` | [#360](https://github.com/kniazkov/g0at/issues/360) |
| `scripts/check_module_delivery.py` → `test/src/functional/module_delivery_scenarios.c` (Python удалён) | [#388](https://github.com/kniazkov/g0at/issues/388) |
| `scripts/check_module_delivery_requirements.py` → `test/src/functional/module_delivery_requirements_scenarios.c` (Python удалён) | [#389](https://github.com/kniazkov/g0at/issues/389) |
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
| `scripts/check_module_musl.py` → `test/src/functional/module_musl_scenarios.c` (Python удалён) | [#390](https://github.com/kniazkov/g0at/issues/390) |
| `scripts/check_module_native.py` | [#347](https://github.com/kniazkov/g0at/issues/347) |
| `scripts/check_module_native_values.py` | [#382](https://github.com/kniazkov/g0at/issues/382) |
| `scripts/check_module_search.py` | [#350](https://github.com/kniazkov/g0at/issues/350) |
| `scripts/check_module_serialization.py` | [#354](https://github.com/kniazkov/g0at/issues/354) |
| `scripts/check_module_snapshot.py` | [#353](https://github.com/kniazkov/g0at/issues/353) |
| `scripts/module_snapshot_fixtures.py` (совместимый слой после #353) | [#354](https://github.com/kniazkov/g0at/issues/354), [#394](https://github.com/kniazkov/g0at/issues/394) |
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
| `scripts/check_package.py` → `test/src/functional/package_scenarios.c` (Python удалён) | [#391](https://github.com/kniazkov/g0at/issues/391) |
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
Они воспроизводили/проверяли исходные выборки и удалены в #395; обычные
потребители только читают `test/data/python-samples/`. Последние исходники
сохранены в Git на `3c1b75bdbe7afa370e47e70c60c0c9a02ffdf57a`, а не как
исполняемые пути текущего дерева. Frozen bytes и manifest не изменены.
Происхождение, форматы, соответствие сценариев и команды проверки описаны в
[`test/data/python-samples/README.md`](../test/data/python-samples/README.md).

Совместимый слой snapshot `scripts/module_snapshot_fixtures.py` удалён в #354 после переноса последнего потребителя identity. Независимый C-конструктор разделяют snapshot и identity: `test/support/module_snapshot_fixture.c`.

### Зависимости финальной очистки (#394)

В #394 удалены `docs/module-format-v1/{reference.py,schema.py,test_reference.py}`
и `scripts/{inspect_report_test_support.py,module_execution_support.py,module_interface_fixtures.py,process_test_support.py}`.
Исторический реестр 89 путей выше сохраняется как карта происхождения, а не
список существующих файлов. Общие C helpers и независимые oracles остаются.

`format_vectors --check/--write --root` сохраняет полный контракт 26 файлов
векторов/opcodes/схем, включая точные проверки opcode map, archive hex и lossy
source. Стабильный `format.reference` теперь агрегирует независимые C-проверки
всех 21 исторических unittest methods. Активные frozen-input gates используют
`frozen_samples --check/--self-test`, независимую проверку manifest/SHA-256,
220 materialized mutation digests и missing/corruption controls; исходные байты
и исторические source hashes неизменны. Это read-only integrity gate, не
повторное выполнение CPython RNG/zlib recipes. Четыре временных reproduction
Python-инструмента выше удалены в #395 без изменения frozen corpus. Полный
Python-free gate и аудит документации относятся к отдельному шагу #395 ниже.

### Итоговый аудит путей и документации (#395)

Исходный реестр выше сохранён без потери исторических имён. Сверка с
`git ls-tree -r --name-only 60d1eb30b2c5cec6bfbf774c9ea518e74c4c160c`
подтверждает все **89** исходных `.py` файлов: 83 имеют те же исторические пути,
а шесть `test/native/checks/{access,libm,runner,runtime_strings,saturation,string_method_errors}.py`
переехали в `test/src/native/checks/` в #320. В реестре показаны их новые пути;
обе формы относятся к одним исходным файлам. Все 89 удалены после своих этапов
переноса; текущих Python runtime-потребителей этих путей нет.
`scripts/module_snapshot_fixtures.py` в исторической таблице — дополнительный
переходный helper после исходного среза, а не девяностый исходный файл.

Проверка всех добавлений `.py` в Git после исходного среза даёт ещё **11**
уникальных путей. Их судьба полностью учтена:

| Дополнительный исторический путь | Удаление / замена |
| --- | --- |
| `scripts/module_snapshot_fixtures.py` | #354, `test/support/module_snapshot_fixture.c` |
| `scripts/check_native_performance_parity.py` | #387, временный original/C gate завершён; `native_performance_scenarios` |
| `test/support/module_delivery_parity.py` | #388, временный original/C gate завершён; `module_delivery_scenarios` |
| `test/support/module_delivery_requirements_parity.py` | #389, временный original/C gate завершён; `module_delivery_requirements_scenarios` |
| `test/support/check_module_musl_parity.py` | #390, временный original/C gate завершён; `module_musl_scenarios` |
| `scripts/check_package_parity.py` | #391, временный original/C gate завершён; `package_scenarios` |
| `scripts/check_module_book_parity.py` | #392, временный original/C gate завершён; `module_book_scenarios` |
| `test/support/freeze_python_samples.py` | #395, read-only `frozen_samples` integrity gate из #394 |
| `test/support/python_samples_binary.py` | #395, frozen bytes сохранены без regeneration |
| `test/support/python_samples_structured.py` | #395, frozen bytes сохранены без regeneration |
| `test/support/verify_original_python_samples.py` | #395, исходные source digests и Git history сохранены |

Встроенный SVG heredoc из `scripts/check_graph.sh` удалён в #393 и заменён
независимым C-validator. Текущие shell/cmd, CMake/CTest и workflows не запускают
Python и не устанавливают его как зависимость проекта. Новые `.py` файлы,
активные interpreter calls, shebangs, imports, `sys.executable` и heredoc
проверяются отдельно от неизменяемых исторических данных.

Оставшиеся упоминания Python делятся на следующие категории:

- Неизменяемые данные и provenance: `test/data/python-samples/`,
  `test/data/integer-oracle/python-results.tsv`, исходные пути/hashes/revisions
  в JSON/TSV и frozen source strings. Это входы C-сценариев, не команды.
- Стабильные CTest aliases `tool.python-frozen-samples` и
  `tool.python-frozen-samples-self-test` вызывают C `frozen_samples`. Старые
  `.py` значения в negative controls native manifest намеренно отвергаются.
- Историческая attribution-строка `scripts/generate_module_json_schema.py`
  в `src/module/module_json_schema.h` сохраняется byte-for-byte C-генератором;
  комментарии C описывают происхождение независимых эталонов. Doxygen содержит
  общий фильтр расширений `*.py`; этот фильтр не запускает интерпретатор.
- Markdown хранит карты переноса, green CI evidence и исторические recipes.
  Рецепты требуют отдельного checkout указанной Git revision со своими helpers,
  а не восстановления удалённых файлов в текущем дереве. Поддерживаемые команды
  сборки/проверки используют C targets и CTest. RU/EN README, API docs и test
  registry не требуют установки Python; Unix shell-наборы по-прежнему требуют Bash.

Активные guard-файлы `scripts/no_python.sh`, `scripts/no_python.ps1`,
`scripts/check_no_python.cmake`, `scripts/check_python_absence.sh` и
`test/support/python_forbidden.c` также называют Python намеренно: они отвергают
интерпретатор и проверяют отсутствие зависимости, а не запускают Python.
Bash/MSYS2 и PowerShell guards сохраняют штатные toolchains и завершают шаг
ошибкой даже при проигнорированном вызове. Disposable Alpine/glibc контейнеры
дополнительно сохраняют package/filesystem inventory без Python. Это не sandbox
для всего hosted runner: установленный вне команд проекта host Python может
оставаться. [Границы доказательства](../test/README.md).

Формат языка, wire bytes, frozen fixtures, schema/vector bytes и historical
source digests не изменены. Отметка реализации пункта 77
добавляется в его PR по правилу 5. Успех полного шага #395 подтверждается только
CI опубликованного окончательного коммита; ссылки добавляются в PR без отдельного
push ради статуса.

#### English audit summary

All 89 baseline Python identities are accounted for, including the six native
paths moved in #320. All eleven Python paths added later are retired as listed
above. Current build, generators, CTest, package and CI entry points use C,
CMake and the retained shell/cmd scripts. No Python source file remains in the
current tree. Historical paths, hashes, generated-header attribution and stable
C aliases are deliberately preserved as data. Historical reproduction requires
a separate checkout of its recorded revision; it is not a current command.
The final published-head CI result, not this source audit alone, closes #395.
