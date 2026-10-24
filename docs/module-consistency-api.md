# Согласованность модулей / Module consistency

## Русский

Этап #233 связывает уже готовые проверки контейнера, переносимого байткода и
нативных записей. `src/vm/module_consistency.h` работает только со снимками
`goat_module_container`, у которых проверены наличие всех библиотек и SHA-256
полных байтов каждой DLL/SO, включая неподходящие платформы. Изменение последнего
байта или добавление данных не скрывается пересчитанным ZIP CRC.

`read_module_consistency` возвращает отдельный неизменяемый образ байткода и
точную входную инструкцию. Проверяются все функции/FUNC, файлы, параметры,
импорты и ссылки специализаций. `goat_module_native_validate` проверяет все цели,
суффиксы библиотек, символы и пересечения условий. У этих API нет флага native:
`off`, `auto`, `required` обязаны пройти один и тот же структурный этап до выбора
варианта. Неизвестная версия ABI или CPU-возможность означает несовместимость,
а не повреждение формата; неправильное сочетание известных требований — ошибка.

`match_module_descriptor` сравнивает безопасно скопированное встроенное описание
с конкретной нормализованной записью архива: имя/версию модуля, основной хеш,
цель, ABI и весь список специализаций. Сравнение каноническое, с точными числами
и длинами строк; порядок условий уже нормализован. Внешние `library`, `hash` и
`cCompiler` не входят во встроенное описание. Здесь нет поиска символа, OS loading
или чтения произвольных адресов: принадлежность mapped ranges проверит #210.

`check_module_references` отдельно проверяет functionRef одного владельца в
предоставленном наборе локально проверенных снимков. Итеративный обход начинается
с его dependencies, каждое ребро сверяет имя, версию и закреплённый основной хеш.
Только достижимая функция точного модуля может удовлетворить ссылку. Номер функции
не является индексом таблицы или глобальным идентификатором. Неоднозначные
идентичности, пропущенные узлы, неверные хеши и отсутствующие функции отвергаются.
Ромб посещает узел один раз. Поиск архивов и запрет циклов принадлежат #189;
эта функция не запускает файлы и не подменяет экземпляры замыканий. Вызывать её
для каждого владельца при проверке полного графа; локальная проверка архива не
требует доступности его внешних зависимостей.

Проверки: `python3 scripts/check_module_consistency.py build/<config>/module_consistency_testing`.
Набор собирает независимые архивы и повреждения, проверяет все три режима,
иностранные варианты, границы ID, расхождения описания и транзитивные ссылки.
Вместо DLL/SO используется намеренно неисполняемый payload: ни один тест не
загружает библиотеку. Существующие ABI/native тесты продолжают проверять реальные
вызовы. Сквозной компилятор модулей, назначение ID нескольким исходным файлам,
исполнение замыканий, привязки clone/flat и пользовательский CLI завершаются в
#186/#191/#211/#232/#219; эта проверка не объявляет их реализованными.

## English

Step #233 joins the existing container, portable-bytecode and native-record
checks. `src/vm/module_consistency.h` accepts owned container snapshots, with all
libraries present and SHA-256 checked over every byte of each DLL/SO, including
foreign variants. Recalculating ZIP CRC cannot hide a changed final byte or
appended data.

`read_module_consistency` returns a separate immutable bytecode image and its
exact main entry. Files, FUNC identities, parameters, imports and specialization
references are checked. `goat_module_native_validate` checks every target, suffix,
symbol and guard intersection. Neither API accepts a native mode: off, auto and
required must share this structural phase before selection. Unknown ABI versions
or CPU features are incompatibilities, not corruption; contradictory known
requirements are malformed.

`match_module_descriptor` compares a safely copied embedded descriptor with one
normalized archive variant: module name/version/main hash, target, ABI and the
complete specialization list. Canonical comparison preserves exact numbers and
string lengths; predicate ordering is already normalized. External library path,
full-library hash and cCompiler are excluded. There is no symbol lookup, OS loading
or arbitrary-pointer probing here; #210 supplies mapped-range ownership checks.

`check_module_references` separately validates one owner's functionRef exports
against supplied locally validated snapshots. An iterative traversal starts from
that owner's dependencies and verifies exact name/version/pinned hash on every
edge. Only reachable functions of the exact module satisfy references. Sparse
function IDs are neither array indices nor global identities. Duplicate module
identities, missing nodes, wrong hashes and absent functions are rejected. A
diamond visits each node once. Archive discovery and cycle rejection remain #189;
this helper neither initializes files nor replaces closure instances. Invoke it
for each owner when checking a complete graph. Local archive validation does not
require external dependencies to be present.

Run `python3 scripts/check_module_consistency.py build/<config>/module_consistency_testing`.
Independent archive/corruption fixtures cover all three modes, foreign variants,
ID boundaries, descriptor mismatches and transitive references. The existing
standalone compiler also preserves nested/anonymous IDs across native off/auto. Library payloads
are deliberately nonexecutable, demonstrating that validation does not load them.
Existing ABI/native tests retain actual calls. Full module compilation, multi-file
producer ID assignment, closure behavior, clone/flat binding and user CLI remain
#186/#191/#211/#232/#219; these metadata tests do not claim those integrations.
