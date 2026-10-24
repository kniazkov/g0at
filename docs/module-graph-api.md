# Iterative dependency preflight (#189)

`vm/module_graph.h` is the shared, nonexecuting whole-graph validation layer for
compiler, loader and inspection consumers. It complements, rather than changes,
`module_dependencies.h`: binding one consumer's direct imports is not a complete
preflight. CLI integration remains #191/#218; presentation and deterministic
sorting of dependency diagnostics remain #190.

`validate_module_graph` reads and validates the root archive, including when its
path differs from its search-path location. `validate_source_graph` accepts
collected exact imports: unused `.gmod` defaults never cause a
lookup, while imports in included files and unreachable branches remain part of
the direct set. Both use one unchanged root-program `module_search_t` for the
entire pass. Libraries never add search paths or inherit application defaults.

## Discovery, identity and errors

Discovery and cycle checking use explicit work structures, not C recursion by
dependency depth. There is no arbitrary depth limit. Each exact name/version is
looked up and validated once per pass, with its actual main hash pinned in the
result; failures are cached too. Different versions remain separate nodes.
Every incoming edge retains its own expected hash, so a successful lookup for one
consumer cannot excuse another consumer's conflicting expectation. Search keeps
its first-candidate policy: no replacement archive is selected after a failure.

Only each archive's immediate `dependencies` are read. A → B → C discovers C
without inserting C into A's metadata. The graph retains adjacency rather than
flattening metadata or copying external payloads into consumers. A shared completed
node is not a cycle: diamonds are valid, active-chain back edges are not.

An invalid branch does not stop independent branches. Missing files, identity and
validation errors, mismatched expected hashes, conflicting hashes and cycles
prevent success. A node without readable, valid metadata is not expanded. Search
errors retain their underlying cause and actually attempted paths. Validated
snapshots are not re-read while processing later incoming edges.

All incoming edges remain available through `module_graph_edge` for diagnostics.
`module_graph_paths` uses an
explicit stack and exposes complete root-to-target chains, including the final
edge's expected hash when reporting conflicts. Shared-node caching therefore does
not erase alternate paths. Enumeration is finite for cyclic input: it enumerates
simple chains rather than unfolding a cycle indefinitely. Passing an exact
`final_edge` appends that edge to every path to its consumer, allowing a repeated
terminal node as a finite cycle witness. The callback can stop enumeration. The number of paths can
be exponential in a graph's size; preserving adjacency avoids materializing all
chains during validation.

Function references are resolved against the validated dependency graph, not by
opening new archives. Their exact module/version and function must be reachable
from the referencing module's own dependencies. A module in a different root
branch cannot satisfy a reference. The preflight neither reruns semantic analysis
nor substitutes runtime closures, initializes modules, extracts libraries or
calls the OS loader. Each independently invalid reference records its export name
in `module_graph_issue_t.reference`. A missing function in an already reachable
valid target is still an error when another branch failed. If a target cannot be
resolved because the owner's reachable graph contains a failed or hash-invalid
edge, the node's `references_skipped` records that unresolved check instead of
inventing an absence error. Skipped checks never make the invalid graph successful.
A reference to the owner itself is not an external function reference.

## Ownership and integration

`MODULE_GRAPH_OK` and `MODULE_GRAPH_INVALID` return an owned result;
`MODULE_GRAPH_ARGUMENT` and `MODULE_GRAPH_MEMORY` clear the output. The result
owns validated snapshots and graph diagnostics. `module_graph_node_t.archive`
retains the diagnostic path/label, including synthetic or invalid roots. The result
can be inspected after input imports and the search object have been released. An invalid result
is diagnostic evidence, **not permission to execute or publish**. Callers must
require a successful full-graph status before those operations. Release the result
with `free_module_graph`; accessor views are borrowed.

Before publishing a named artifact, the compiler must also call
`validate_module_graph` on its final temporary archive, reusing the **same original
search object**. Source preflight has a synthetic root and authorizes dependency
analysis only: it cannot establish the final artifact's identity. The final pass
catches a dependency that imports an old installed copy of the application's own
name/version, which becomes a back edge to the actual archive root. This validates
the final artifact anew; caching is per pass, not across compilation phases.
CLI/compiler wiring of this publication gate remains #191.

Core allocation follows the existing fatal-OOM policy; loader-wide OOM behavior remains #236.

The shared Linux/Windows component fixtures exercise transitive discovery,
coexisting versions, diamonds and cached failures, deep chains, cycles, independent
bad branches, hash conflicts and alternate paths, and function-reference
reachability. No test requires executing imported user or native code.

# Итеративная предварительная проверка зависимостей (#189)

`vm/module_graph.h` — общий неисполняющий слой проверки полного графа для
компилятора, загрузчика и инспектора. Он дополняет `module_dependencies.h`:
привязка непосредственных импортов одного потребителя не заменяет полный проход.
Подключение CLI остаётся в #191/#218, представление и детерминированная сортировка
диагностики — в #190.

`validate_module_graph` читает и проверяет корневой архив, даже если он находится
не в каталоге поиска. `validate_source_graph` принимает собранные точные импорты. Неиспользованные значения `.gmod` не вызывают поиск; импорты из
include и недостижимых ветвей сохраняются. Во всём проходе используется один
неизменный `module_search_t` корневой программы. Библиотеки не добавляют каталоги
и не получают настройки версий приложения.

## Обход, идентичность и ошибки

Раскрытие зависимостей и проверка циклов используют явные рабочие структуры без
рекурсии C по глубине графа и без искусственного предела глубины. Каждая точная
пара имя/версия ищется и проверяется один раз за проход, её фактический основной
хеш закрепляется в результате; ошибки также кешируются. Разные версии остаются
разными узлами. Каждое входящее ребро сохраняет собственный ожидаемый хеш:
успешная проверка для одного потребителя не скрывает конфликт другого. Правило
первого кандидата сохраняется — после ошибки другую копию не выбирают.

Читаются только непосредственные `dependencies` каждого архива. В A → B → C
модуль C обнаруживается без добавления записи C в A. Граф сохраняет связи, не
уплощает метаданные и не встраивает чужие данные в потребителя. Уже проверенный
общий узел ромба допустим; обратное ребро активной цепочки означает цикл.

Неисправная ветвь не останавливает независимые. Отсутствие файла, ошибки
идентичности и проверки, несовпадение ожидаемого хеша, конфликт хешей и цикл
запрещают успех. Узел без доступных корректных метаданных не раскрывается. Ошибки
поиска сохраняют исходную причину и действительно проверенные пути. Повторное
входящее ребро не вызывает повторного чтения проверенного снимка.

Для диагностики сохраняются все входящие рёбра (`module_graph_edge`).
Итеративное перечисление путей (`module_graph_paths`)
выдаёт полные цепочки от корня, а для конфликта позволяет связать путь с ожидаемым
хешем последнего ребра. Кеш не скрывает альтернативные цепочки. На циклическом
входе перечисляются конечные простые цепочки, а не бесконечные развёртки цикла.
Параметр `final_edge` добавляет точное последнее ребро к путям его потребителя,
допуская повтор конечного узла как свидетельство цикла. Обработчик может остановить
перечисление. Число путей может быть экспоненциальным: хранение связей не требует заранее
материализовать все цепочки при проверке.

functionRef разрешается в проверенном графе без нового поиска архивов. Указанные
модуль, точная версия и функция должны быть достижимы из собственных зависимостей
владельца ссылки; другой ветви корневого графа недостаточно. Проверка не повторяет
семантический анализ, не подменяет замыкания, не инициализирует модули, не извлекает
библиотеки и не вызывает загрузчик ОС. Каждая независимо некорректная ссылка
сохраняет имя экспорта в `module_graph_issue_t.reference`. Отсутствующая функция
в уже достижимом корректном модуле остаётся ошибкой, даже если другая ветвь
неисправна. Если цель не удалось разрешить при наличии неисправного ребра
(в том числе неверного ожидаемого хеша) в достижимом подграфе владельца,
`references_skipped` считает непроверенную ссылку вместо недоказанной ошибки
отсутствия. Пропуск проверки не превращает неисправный граф в успешный.
Ссылка на самого владельца не считается внешней ссылкой на функцию.

## Владение и подключение

`MODULE_GRAPH_OK` и `MODULE_GRAPH_INVALID` возвращают результат во владение
вызывающему; `MODULE_GRAPH_ARGUMENT` и `MODULE_GRAPH_MEMORY` очищают выход.
Результат владеет проверенными снимками и диагностикой. `module_graph_node_t.archive`
сохраняет диагностический путь/метку, в том числе для синтетического или неисправного
корня. Результат переживает освобождение
импортов и поискового объекта. Неуспешный результат нужен только для диагностики:
**исполнение и публикация запрещены**, пока полный граф не проверен успешно.
Представления из аксессоров заимствованы, результат освобождается деструктором
`free_module_graph`.

Перед публикацией именованного артефакта компилятор обязан также вызвать
`validate_module_graph` для окончательного временного архива, передав **тот же
исходный поисковый объект**. Исходная проверка имеет синтетический корень и
разрешает только анализ зависимостей, но не устанавливает идентичность готового
артефакта. Финальный проход обнаружит зависимость, импортирующую старую установленную
копию приложения с той же парой имя/версия: для окончательного архива это обратное
ребро в настоящий корень. Готовый артефакт проверяется заново; кеш действует внутри
прохода, а не между фазами компиляции. Подключение этого барьера публикации остаётся
в #191.

Сохраняется общая аварийная политика выделения памяти;
поведение загрузчика при нехватке памяти относится к #236.

Общие компонентные тесты Linux/Windows проверяют транзитивное раскрытие, разные
версии, ромбы и кеширование ошибок, глубокие цепочки, циклы, независимые ошибочные
ветви, конфликты хешей и альтернативные пути, достижимость functionRef. Исполнять
пользовательский или нативный код зависимостей для этих тестов не требуется.
