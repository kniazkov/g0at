# Consumer-local dependency binding (#238)

`vm/module_dependencies.h` connects static direct import collection (#187) with
checked archive lookup (#188/#237). It binds **one consumer at a time**, using the
same root-program search object. Neither operation executes code or loads native
libraries.

* `bind_source_dependencies` accepts collected exact pairs, validates uniqueness,
  sorts by name/version, and returns each fully verified snapshot plus its actual
  main hash. Unused `.gmod` defaults are not searched, emitted, or embedded; the
  collector's `unused_defaults` remain warnings. Imports in unreachable branches
  and included files are already part of the collector's direct set.
* `bind_module_dependencies` accepts a validated library snapshot, reads **only
  that library's** `dependencies`, and checks its stored exact names, versions and
  expected hashes. There is deliberately no application-descriptor parameter.
  An application's `math@2` cannot replace the library's `math@1`, nor can a
  successful application lookup excuse the library's conflicting expected hash.

Both return an owned canonical JSON array of strict `{name,version,hash}` records
and matching immutable dependency snapshots. The array is suitable for the
consumer's `module.json.dependencies`; only that consumer's direct records belong
there. Dependency bytecode/native payloads and the dependency's own records are
not copied into the consumer. A library binding is **not** a complete-graph
validation: callers must also process each returned snapshot's own direct records.
A → B → C thus retains B in A and C in B; importing C directly (including via an
own include) adds C to A as well.

This primitive intentionally does not implement whole-graph traversal. Use the
[iterative graph API](module-graph-api.md) for node caching, cycle checking and
retained diagnostic paths (#189); diagnostic presentation remains #190. A caller
must never treat a successful single-consumer binding as permission to execute or
publish an unverified complete graph. CLI integration remains #191/#218.

## Ownership and failures

Inputs remain unchanged; returned records and snapshots survive destruction of
sources, imports, the consumer snapshot and search object. Release with
`free_module_dependencies`. Views/accessors are borrowed; invalid handles/indices
return NULL or zero. Initialize errors to zero and release using
`clear_module_dependencies_error`; reuse clears prior details. NULL errors work.
A search failure gives the canonical edge index and the full underlying search
error. Other statuses distinguish invalid input from JSON allocation failure.
Core allocation still follows fatal-OOM policy; recoverable loader OOM is #236.
Failure clears the result and releases all previously resolved direct dependencies.

`ctest --test-dir build --output-on-failure -R '^integration.module_dependencies$'` runs in
Linux/Windows CI alongside existing schema/import/hash tests. Fixtures inspect
per-consumer records and manually follow edges to verify coexistence of versions,
transitive expected-hash failure, no flattening, and independently owned snapshots.
Existing schema tests reject swapped `.gmod`/metadata dependency forms, duplicates,
invalid hashes/fields and invalid unused defaults; canonical/hash tests enforce
unordered dependency sets while retaining the authors ordering contract.

# Привязка зависимостей отдельного потребителя (#238)

`vm/module_dependencies.h` связывает непосредственные импорты с проверенными
архивами. `bind_source_dependencies` получает точные пары от сборщика импортов и
записывает фактические проверенные основные хеши. Неиспользованные значения из
`.gmod` остаются предупреждениями: их архивы не ищутся, записи не сериализуются.
Импорты в невыполненных ветках и собственных включённых исходниках учитываются.

`bind_module_dependencies` читает только зафиксированные записи самой библиотеки,
проверяя её версии и ожидаемые хеши. Дескриптор приложения этой функции не
передаётся. Поэтому math@2 приложения не подменяет math@1 библиотеки, а успешная
проверка зависимости приложения не скрывает несовпадение хеша у библиотеки.
На всех уровнях используется один поисковый объект корневой программы.

Результат владеет отсортированным по имени/версии массивом `{name,version,hash}` и
проверенными снимками. В метаданные потребителя входит только этот массив:
байткод/нативный код внешних библиотек и их транзитивные записи не встраиваются.
Для A → B → C массив A содержит B, массив B содержит C. Собственный import C
(включая import из include) добавляет C в A независимо от транзитивного пути.

Операция проверяет один уровень, а не весь граф. Для итеративного обхода, кеша,
циклов и сохранения цепочек используется [API графа](module-graph-api.md) (#189).
Представление диагностики остаётся в #190; CLI — в #191/#218.
До исполнения или публикации необходима проверка всего достижимого графа.
Код и OS loader здесь не запускаются. Результат переживает освобождение всех
входов, освобождается через `free_module_dependencies`; при ошибке частичного
результата нет. Ошибка поиска сохраняет индекс непосредственного ребра в
каноническом порядке и исходные подробности поиска. Ошибки инициализируются нулём,
очищаются `clear_module_dependencies_error`; NULL допустим. Ошибки аргументов и
выделения JSON-памяти различаются; общая политика нехватки памяти остаётся #236.

Общие тесты Linux/Windows проверяют две версии одного имени, неизменность записей
библиотеки, конфликт ожидаемых транзитивных хешей, отсутствие уплощения и владение
снимками. Полный обход не имитируется как уже готовый механизм: тест последовательно
вызывает API для отдельных потребителей и проверяет точные рёбра каждого.
