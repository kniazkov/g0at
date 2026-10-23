# Сервис уборки / Cleanup service

## Русский

`lib/cleanup_service.h` — общий сервис #245 для будущих потребителей #210, #211,
#214 и #230. Он не реализует режимы auto/required, загрузку модулей или язык exit.
Контекст создаётся один раз на запуск владельца и разделяется его потребителями.
Создание контекста не запускает процесс и не создаёт каталог. Хост вызывает
`cleanup_service_main(argc, argv, &code)` до обычного разбора CLI и немедленно
возвращает `code`, если обработан внутренний режим. В Goat этот вход уже подключён;
обычная работа пока не обращается к сервису автоматически.

`ensure_cleanup_service` запускает один независимый уборщик. Конкурентные первые
вызовы разделяют одну попытку и общий монотонный срок **1 с**, отсчитываемый до
создания каналов и процесса. READY означает: уборщик проверил устойчивый дескриптор
исходного владельца, создал собственный случайный каталог и готов обслуживать его.
Возвращаемый `cleanup_service_directory` — заимствованный абсолютный UTF-8 путь до
уничтожения контекста. До успешной готовности потребитель не создаёт временные C,
DLL/SO, промежуточные файлы и не запускает компилятор. Пустой каталог и служебные
каналы могут существовать во время подготовки. Поздний READY не отменяет отказ;
ACK с тем же сроком завершает согласование. Отказ фиксируется, повторов запуска нет.
Потребитель auto предупреждает один раз и выбирает VM до конца запуска; required
возвращает ошибку. Эти политики интегрируются задачами потребителей.

`run_cleanup_process` синхронно передаёт уборщику executable, argv и абсолютный
срок процесса. Весь вывод ограниченно захватывается, stdin пуст. Остальные поля
`child_options_t` должны быть нулевыми. Результат процесса копируется в выходной
`child_result_t`; CLEANUP_OK здесь означает доставленный результат, а не нулевой
exit code. Это **регистрация до запуска**, а не передача PID уже запущенного
компилятора: дочернее дерево создаётся непосредственно уборщиком через #213.
Если владелец погибает в фазе запуска, уборщик завершает созданное дерево до
удаления. Произвольные внешние PID не принимаются. Вызовы сериализуются; память
argv можно освобождать после возврата. Потребитель передаёт абсолютные пути своих
временных файлов. Рабочий каталог процесса сейчас наследуется от уборщика;
изолированный cwd пробы должен быть добавлен при интеграции #214.

Уборщик наблюдает за **фактическим завершением владельца**, используя Linux pidfd
или наследованный Windows process HANDLE. Закрытие клиентского канала либо
`destroy_cleanup_service` не означает завершения владельца и не запускает
удаление используемых файлов. Уничтожение контекста освобождает только клиентские
ресурсы и не ждёт уборки. В Linux служебный supervisor отсоединяется двойным fork;
в Windows служебный Job не имеет kill-on-close. Обычные дочерние деревья сохраняют
прежние правила остановки. Убийство самого уборщика после READY не отзываeт
готовность и не меняет код владельца; новые запросы возвращают явную ошибку связи.
Компенсирующего удаления или выгрузки библиотек в владельце нет.

`finish_cleanup_service` используется inspect **после прекращения использования
и выгрузки ресурсов**. Он отправляет «убрать сейчас» и ждёт окончательный результат
не более **5 с** от начала запроса, включая ожидание сериализации и передачу.
Повторы возвращают сохранённый результат, не запускают новую уборку. Истечение
срока не отменяет работу уборщика. Пока синхронный запуск занят другим вызовом,
запрос может исчерпать бюджет до отправки; тогда уборка начнётся при завершении
владельца. Обычному Goat вызывать finish перед выходом не требуется.

Удалением владеет только уборщик. Он сначала прекращает своё активное дерево и
подтверждает отсутствие потомков. Если #213 сообщает `cleanup_error`, файлы
остаются и возвращается CLEANUP_TREE_FAILED. Ошибка проверки дескриптора владельца
также не разрешает удаление. Затем #249 удаляет только собственный каталог без
перехода по ссылкам. Повторяемые ошибки удаления имеют общий монотонный бюджет
**30 с от начала очистки**, включая остановку дерева, с паузами до 50 мс. Частичное
удаление и смерть владельца не обновляют срок. Неверный путь и неподдерживаемая
операция дают немедленный отказ. FS_NOT_FOUND считается успешной уборкой.
Один последний вызов удаления может выйти за срок при зависшем системном вызове:
бюджет ограничивает повторы, а не обещает прерывание ядра/файловой системы.

После результата уборщик завершается, даже если клиент исчез. Остатки предыдущих
запусков и соседние каталоги не сканируются. Сохранённые артефакты нужно размещать
вне собственного временного дерева. API не является песочницей и не делает
небезопасный код безопасным; действуют ограничения файлового API и #213
(в Linux нужен procfs соответствующего PID namespace для учёта потомков).

При отказе или таймауте клиент сохраняет известный путь отдельно от результата.
Будущий отчёт inspect передаёт его в `temporaryDirectory`; неизвестный путь
отсутствует. Таймаут не доказывает остановку уборщика и не разрешает немедленное
ручное удаление. Ошибки сервиса не печатаются автоматически и не выбирают exit code
программы. Требования к отмене/повторам и владению распространяются на все режимы.

Проверки: `scripts/check_cleanup_service.py <cleanup_service_testing>` запускает
изолированные владельцы и напрямую проверяет сервис, без готовых потребителей.
Используются READY/каналы/маркеры и устойчивые handles процессов, внешние watchdog;
проверка повторов и границ 30 с использует управляемые часы. Linux/Windows CI
включает этот стенд отдельно. Новые механизмы transport/poll/release также
проверяются существующим стендом #213.

## English

`lib/cleanup_service.h` implements #245 for consumers #210, #211, #214 and #230.
It does not implement auto/required policy, module loading or language exit.
Create one shared context per owner run. Context allocation creates neither a
process nor a directory. The host calls `cleanup_service_main` before ordinary
CLI parsing and immediately returns its exit code when handled. Goat already
includes this entry point; ordinary consumers are integrated in later tasks.

`ensure_cleanup_service` shares a single launch attempt and **one monotonic 1 s
budget**, starting before channel/process creation, among concurrent first users.
READY means the cleaner validated a stable owner process reference, created its
own random directory and can service it. `cleanup_service_directory` borrows an
absolute UTF-8 path until context destruction. No temporary C, DLL/SO, intermediate
files or compiler launch is permitted before successful readiness. An empty
directory and control channels may exist during preparation. ACK uses the same
deadline; late readiness never revokes a sticky failure. There are no relaunches.
Consumers implement one warning plus run-wide VM fallback in auto, or a startup
error in required.

`run_cleanup_process` synchronously delegates executable, argv and an absolute
process deadline. Output capture is bounded and stdin is empty; other
`child_options_t` fields must be zero. CLEANUP_OK means the child result was
received, not that its exit code was zero. The cleaner launches the owned tree
through #213 itself, establishing ownership **before launch**, rather than
registering a bare PID afterwards. Owner death during launch cannot create an
unregistered compiler. Calls are serialized; argument memory may be released
on return. Use absolute temporary paths. Working directory currently inherits
from the cleaner; #214 integration must add an isolated probe cwd.

The cleaner watches actual owner death via Linux pidfd or an inherited Windows
process HANDLE, never a reused numeric PID. Channel closure and
`destroy_cleanup_service` release client state without deleting files or waiting
for cleanup; they do not mean the owner exited. Linux service supervisors use
double fork, Windows service jobs omit kill-on-close. Ordinary child tree
containment is unchanged. Cleaner death after READY does not revoke readiness or
change the owner's exit code. Subsequent requests report connection loss; no
client-side compensating deletion or library unload occurs.

Inspect calls `finish_cleanup_service` only **after releasing/unloading its
resources**. The final result has a separate **5 s** deadline including lock
acquisition and request transmission. Repeated calls retain the result and never
restart cleanup. Timeout does not cancel the cleaner. A concurrent synchronous
launch may hold serialization until this budget expires before sending; cleanup
then starts on actual owner death. Ordinary Goat need not call finish at exit.

Only the cleaner deletes. It first stops its active child tree and confirms
termination. A #213 cleanup error yields CLEANUP_TREE_FAILED and leaves files;
an unusable owner watch likewise cannot authorize deletion. The #249 filesystem
API removes only the owned directory without following links. Retriable failures
share **30 s from cleanup start**, including tree shutdown, with pauses of at most
50 ms. Partial progress and owner death never reset the budget. Invalid paths and
unsupported operations fail immediately; FS_NOT_FOUND is success. The budget
bounds retries, not an uninterruptible filesystem/kernel call.

The cleaner exits after reporting the result, even if the client disappeared.
It never scans leftovers or neighboring directories; explicitly saved artifacts
must live outside its temporary tree. This is not a sandbox: #249 and #213
limitations apply, including Linux procfs in the matching PID namespace for
tracking descendants. Consumers retain the known path separately from status;
inspect will expose `temporaryDirectory`, omitting it when unknown. Timeout does
not prove cleaner termination or make immediate manual deletion safe. The service
prints no diagnostics and does not select user exit codes.

Run `scripts/check_cleanup_service.py <cleanup_service_testing>` for direct,
isolated service tests. Readiness/markers and stable process handles synchronize
the fixtures, with external watchdogs; retry-budget boundaries use an injected
clock. Linux/Windows CI runs this suite and the existing #213 tests independently.
