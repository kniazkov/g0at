# Единая проверка .gbin / Unified .gbin validation

## Русский

Этап #185 предоставляет CMake-библиотеку `goat_validation` и API
`src/vm/module_validation.h`. Компилятор, загрузчик, inspect и merge используют
один и тот же `validate_module` (байты) или `validate_module_file` (UTF-8 путь).
Будущие CLI-интеграции не вводят отдельного валидатора. Схемы и эталоны находятся
в `docs/module-format-v1`: schema JSON, `reference.py` и `vectors`; эта библиотека
использует те же сгенерированные таблицы схем, нормализацию и хеширование.

Успешный `validated_module_t` владеет нормализованными метаданными, полными
распакованными байтами всех библиотек и отдельным неизменяемым образом байткода.
Входной буфер можно изменить/освободить после возврата. Изменение или удаление
исходного пути не меняет снимок. Геттеры дают только заимствованные const-view,
действующие до `free_validated_module`; главный вход выбран через проверенную
таблицу файлов. Ошибка всегда обнуляет выход, частичный снимок не публикуется.

Лимиты входного архива и суммы распакованных размеров независимы. NULL options
означает предел адресного пространства, а явно заданный ноль — нулевой лимит.
Ошибки I/O, аргументов, лимитов и памяти различаются с повреждением. Низкоуровневый
декодер байткода пока сохраняет fatal-OOM политику core; #236 отдельно определяет
поведение всего загрузчика при нехватке памяти.

Report callback синхронно получает архив, ограниченное длиной имя ZIP-записи,
JSON pointer и исходные коды ошибок. Данные callback заимствованы только на время
вызова. `FAILED`, `SKIPPED` и `PASSED` различаются перечислением; skipped содержит
этап-предпосылку, включая внутренний этап контейнера. Порядок детерминирован:
структура ZIP, CRC записей в порядке central directory, JSON/схема, объявления
библиотек, их SHA-256, основной SHA-256, байткод, native. Внутри схемы обход следует
порядку полей схемы/входного объекта, неизменному для того же входа.

Ошибка CRC одной записи не скрывает другие CRC. Синтаксическая ошибка JSON не
порождает выдуманных ошибок полей/основного хеша. Независимые ошибки типов в
корректном JSON собираются вместе. Семантические проверки безопасно разобранных
частей продолжаются и при ошибке хеша; успех одного этапа не делает весь архив
пригодным. Непроверенные этапы не объявляются успешными.

Никакой Goat-код, анализатор доказательств, OS loader или конструктор DLL/SO не
вызывается. Нет извлечения на диск, поиска зависимостей или режима native: все
варианты, в том числе чужие, проверяются одинаково. Внешние зависимости и
functionRef проверяются локально как записи; доступность и транзитивный граф
проверяются отдельно. Хеши подтверждают целостность, а не истинность гарантий
производителя. Несовместимость неизвестных ABI/CPU остаётся несовместимостью,
не повреждением. Повреждение никогда не превращается в native fallback.

Проверки: `python3 scripts/check_module_validation.py build/module_validation_testing`.
Общие fixtures покрывают память/файл, владение снимком, CRC/ZIP/схему/хеши,
пересчитанные хеши повреждённого байткода, ссылки и множественную диагностику.
Потребители пока проверяются через общий API; запуск будущих пользовательских
утилит относится к их собственным задачам, CTest не добавлен.

## English

Step #185 provides the CMake `goat_validation` library and
`src/vm/module_validation.h`. Compiler, loader, inspect and merge share
`validate_module` (bytes) or `validate_module_file` (UTF-8 path), rather than
forking validators. The exact schemas/reference vectors remain in
`docs/module-format-v1`; generated schema tables, normalization and hashing are
reused, not redefined.

A successful `validated_module_t` owns normalized metadata, all expanded library
bytes and a separate immutable bytecode image. Inputs may be changed/freed after
return; replacing/removing the original path cannot change the retained snapshot.
Const getters borrow views until `free_validated_module`. The main instruction
comes from checked file metadata. Any failure clears the output; no partial
snapshot is published.

Archive and expanded-size limits are independent. NULL options use address-space
limits; explicit zero means zero. I/O, argument, limit and memory failures differ
from corruption. The underlying bytecode decoder still has core's fatal-OOM
policy; #236 addresses loader-wide memory failure handling separately.

Synchronous reports carry archive labels, bounded ZIP names, JSON pointers and
original error codes. Callback views last only for that callback. FAILED,
SKIPPED and PASSED are structural enum states; skipped checks identify their
failed prerequisite, including the inner container stage. Stable order is ZIP
layout, per-entry CRC in central-directory order, JSON/schema, declarations,
library SHA-256, main SHA-256, bytecode and native metadata. Schema traversal is
stable for identical input. Independent CRC/type/hash failures accumulate;
malformed JSON suppresses dependent schema/hash checks without invented errors.
Safe semantic checks continue despite hash mismatches; an individual pass never
makes a corrupt archive executable.

There is no execution, proof re-analysis, OS loading, extraction, dependency
search or native-mode option. All variants, including foreign ones, receive the
same structural checks. External dependencies/functionRef are local records;
availability and transitive graph checks are separate. Hashes establish integrity,
not producer-proof truth. Unknown ABI/CPU requirements remain incompatibilities,
not corruption, and corruption never becomes native fallback.

Run `python3 scripts/check_module_validation.py build/module_validation_testing`.
Shared fixtures cover memory/file inputs, snapshot ownership, ZIP/CRC/schema/hashes,
rehashed malformed bytecode, references and accumulated/skipped diagnostics.
Future consumer CLIs retain their own integration tasks; no CTest was introduced.
