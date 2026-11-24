# Аудит границ GC и embedding (#635)

Это актуальный контракт после #633/#634. Хронологические разделы
memory-management.md описывают этапы реализации, а не дополнительные разрешения.
Автоматические пороги остаются отдельными задачами #638/#639.

## Допуск и владение

`configure_gc_boundaries` включает принудительные проверки только при простое,
под model lock и при обещании исключительного владения со стороны host. Это не
разрешение произвольным callbacks. Обычная инструкция имеет один запрет executor;
проверенные initializer-кадры отдельно учитывают добавочную глубину lock.
`gc_call_wait_begin` требует соответствия thread/session, ровно одного запрета,
отсутствия публикации, согласованного учёта initializer и глубины lock
`1 + gc_call_extra_lock_depth`. Отказ сохраняет запрет. Перед изменением managed
состояния публикация снимается.

`process_execution_suspend/resume` лишь отпускает/восстанавливает рекурсивный mutex.
Он не даёт разрешения GC. INCREF сам по себе не публикует C-local для tracing.
Разрешающий сборку владелец обязан зарегистрировать невидимые managed-значения
владеющими корнями. Borrowed cells/topology/строки/очереди не переживают отпускание
lock без проверенного контракта владения и изменения данных.

## Матрица

Пути указаны от корня репозитория; семейства тестов — имена CTest.
В каждой строке указан проверенный допуск либо явный запрет.

| Путь / граница | Владельцы и решение о lock/публикации | Проверки |
|---|---|---|
| `src/model/process.c`: enter/leave/suspend/resume | Только mutex; восстановления глубины недостаточно для допуска | `core.gc_protocol`, `core.gc_embedding` |
| `src/vm/vm.c`: dispatcher | Корни стека/context; executor запрещает GC, завершённая инструкция публикуется при точной глубине | `core.gc_runtime`, `core.gc_dispatch` |
| `vm.c`: CALL/CALLM, операторы | Корни callable/receiver/снятых аргументов; произвольный builtin исключён, допуск только конкретным проверенным функциям | `origin-*`, `operator-*`, `core.gc_embedding` |
| `vm.c`: property/bound/origin preflight | Стек/context удерживают значения; ожидание до заимствования cells, повтор поиска после пробуждения | `property-*`, `bound-*`, `origin-*` |
| `src/model/module_files.c`: публикация scope и initializer gate | Registry владеет scope; observer учитывает waiter; model lock отпускается до gate; неизвестный caller исключён | `core.gc_module_initialization.*`, включая recursive |
| `vm.c`: execute_module_file, initializer frames | Saved exception в корне, frame/scope/caller сохранены; глубина lock/executor восстанавливается при ошибке | `core.gc_runtime`, initializer suites |
| `src/model/function.c`: scalar-v2 invoke | Аргументы на VM-стеке; native allocations unmanaged; публикация только на время вызова | `core.gc_runtime_native.*`, `core.gc_native_wait.*`, `core.gc_native_startup.*` |
| `src/model/module_native.c`: scalar-v1 invoke | Корни аргументов и стабильный binding; результат конвертируется после снятия публикации; ошибка не повторяет тело | module adapter cases в `core.gc_runtime` |
| Host exception обоих ABI | Model lock берётся, публикация приостанавливается на изменение исключения, затем восстанавливается; произвольный reentry не разрешается | host-exception варианты native-wait/runtime |
| `module_native.c`: resolver | По умолчанию запрет; допуск только явно проверенному binding, настроенному при простое | resolver cases в `core.gc_runtime` |
| `src/vm/module_loading.c`: loader mutex/I/O, report/write | Snapshot/library стабильны на run; model lock отпускается до loader mutex; custom writer/report запрещают публикацию resolver | loading/resolver suites, `core.gc_runtime` |
| `vm.c`: continuation driver/step | Публикуются завершённые шаги; значения на VM-стеке, driver не держит managed locals; ошибки/отмена без replay | `core.gc_continuation.*` |
| `src/builtins/{print,println,input}.c` | Корни receiver/аргументов, unmanaged снимок вывода; публикация на I/O, снимается до преобразования в модель | `core.gc_io.*` |
| Observation/copy (`gc_protocol.c`, `user_defined_object.c`) | Source в корне, borrowed обход повторяется после ожидания; render/copy под lock вне публикации | `observation-*`, `copying-*` |
| `vm.c`: join/terminal condition waits | Registry держит результат/ошибку потока; аргумент join в корне; initializer join запрещён; terminal только при глубине один | runtime wait/startup/terminal cases |
| `vm.c`: schedule_and_reap | Записи завершённых потоков остаются в registry; OS join вне model lock; незавершённый reap блокирует GC | runtime/dispatch worker cases |
| `run` / `run_module_file` активного процесса | Отказ до изменения execution/session/context/стека; другой процесс может работать, но внешний C-кадр остаётся исключённым | `core.gc_embedding` |
| Непроверенный C-callback, дополнительные host locks | Явный запрет даже после suspend/resume и вложенного исполнения; запрос ждёт безопасной границы | `core.gc_embedding`, `core.gc_protocol`, recursive runtime/native |

Учтены все production-вызовы `process_execution_suspend` и condition waits на
execution mutex. Обычный leave при возврате из API/завершении worker заканчивает
владение, а не создаёт отдельную безопасную точку. Остальные ожидания compiler и
cleanup service владеют unmanaged-состоянием и сами не публикуют участника GC.

## Прогресс и ограничения host

Произвольный callback может задерживать GC неограниченно, в том числе при host I/O.
Дополнительный lock и рекурсивное исполнение не делают его locals видимыми.
Это явный поддерживаемый отказ, а не обещание ограниченной паузы. Host должен
вернуться к проверенной границе; нельзя форсировать `collect_garbage` или подделывать
поля participant из callback.

Запросы объединяются и сохраняются при отказе. Новая матрица из шести вариантов
проверяет 0/1/2 дополнительных lock, обычный/исключительный выход, отказ повторного
standalone/module run, вложенный другой процесс, цикл только в C-local и сборку
после возврата. Проверяются состояние caller, неизменность байткода и баланс
аллокатора/корней. Нельзя создавать циклический порядок lock разных процессов;
в тесте используется только рекурсивное владение одним OS-потоком.

## Критерии #617 и остаток

- Standalone/module VM и вложенные функции: runtime; запрет вложенного executor:
  dispatch; сохранённое состояние: continuation/initializer. Finally — runtime,
  снимки конструкторов — существующие `component.init_snapshot.*`.
- Оба scalar ABI, borrowed строки, host exception и native errors: runtime adapter,
  scalar-native/native-wait. Ошибки/отмена continuation — `core.gc_continuation.*`.
  После входа в native тело VM не повторяет его.
- Workers/bootstrap/join/terminal/reap: runtime/dispatch/I/O/startup. Ожидания
  модулей, свойств, операторов и копирования: initializer family.
- Произвольные callbacks, рекурсивный host и deferred request: protocol/runtime
  плюс `core.gc_embedding`.

Аудит не даёт оснований расширять production-допуск. После слияния #635 можно
сверить дочерние реализации #617; объединённая приёмка остаётся в #637. Далее
#638/#639 — автоматическая политика, #640 — pressure, #641 — fatal OOM,
#642/#643 — стресс/итоговый CI. Ограниченное время ожидания при невозвращающемся
host callback не обещается и не предполагается молча.
