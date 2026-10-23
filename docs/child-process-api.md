# Дочерние процессы / Child processes

## Русский

`src/lib/child_process.h` — общий API Linux/Windows 10 для запуска одного исполняемого файла без shell. `executable = NULL` запускает текущий исполняемый файл. Аргументы и пути имеют кодировку UTF-8; Windows использует `CreateProcessW` и экранирование CRT. `start_child_process` возвращает владеющий объект; `wait_child_process` обслуживает потоки и возвращает результат, `stop_child_process` принудительно завершает дерево, `destroy_child_process` освобождает объект (сначала останавливая незавершённый запуск). Один объект обслуживается одним потоком.

`child_deadline_after(ms)` создаёт абсолютный монотонный срок; ноль означает отсутствие ограничения. Срок создаётся **до** запуска и передаётся всем командам одной пробы, включая запрос сведений, компиляцию и линковку. Печать, сообщения и переход к следующей команде не обновляют срок. После таймаута поздний успех не принимается. Остановка и подтверждение уборки имеют отдельный ограниченный период; неисправность уборки возвращается в `cleanup_error`. Ненулевой `cleanup_error` запрещает потребителю удалять файлы, которые могли остаться в использовании.

`capture_output` раздельно перехватывает stdout/stderr с запуска. Первые 65536 байт каждого сохраняются точно, включая NUL и неверный UTF-8. Только следующий байт устанавливает `truncated`; остальное читается и отбрасывается. Одна итерация чтения ограничена, чтобы непрерывная печать не блокировала проверку срока. Вывод доступен и при аварии/таймауте. Общий порядок между потоками не определён. Ошибки чтения возвращаются в `system_error`, независимо от статуса процесса.

`result_channel` создаёт третий отдельный ограниченный поток. Точный аргумент `CHILD_RESULT_ARGUMENT` заменяется десятичным значением дескриптора/handle; дочерний процесс использует `child_write_result` и `child_close_channel`. Обычный вывод не является служебным протоколом. JSON, Base64 и экранирование вывода — обязанности будущего inspect (#214/#223).

`empty_stdin` открывает `/dev/null` или `NUL` до выполнения дочернего кода: штатное чтение возвращает EOF, не затрагивая stdin родителя. По умолчанию stdin наследуется. `parent_watch` передаёт через `CHILD_PARENT_ARGUMENT` устойчивую ссылку на родителя: Linux pidfd (ядро 5.3+) или Windows process handle. `child_wait_parent` не использует голый PID. Если механизм недоступен, запуск завершается явной ошибкой.

Linux запускает отдельного наблюдателя-subreaper до запуска рабочего процесса. Управляющий socket уведомляет его о смерти владельца даже в фазе запуска. Наблюдатель сохраняет PID корня до остановки группы, усыновляет осиротевших потомков и завершает только своих ещё не собранных детей, включая потомков с новой session. Для перечисления используется `/proc/thread-self/children`; нужен доступный procfs соответствующего PID namespace. Ошибка доступа явно означает неподтверждённую уборку. Windows атомарно включает процесс в Job Object через `PROC_THREAD_ATTRIBUTE_JOB_LIST` при `CreateProcessW`: нет окна между созданием и присоединением. Job не допускает breakaway и обычно имеет kill-on-close; наследуется только явный список стандартных/служебных handles. После любого исхода корня оставшиеся потомки прекращаются до возврата результата.

Явный `survive_parent` предназначен для независимого процесса сервиса уборки #245. Его смерть владельца не останавливает; сервис ждёт `parent_watch` и владеет своими рабочими деревьями через обычные объекты. На Windows этот режим отключает kill-on-close для самого сервиса: при исчезновении его владельца за собственную работу отвечает сервис, а не уже исчезнувший вызывающий процесс. Обычный запуск этот режим не использует. Это управление владением, не песочница против намеренно враждебного native-кода.

Существующая сборка DLL/SO использует общий API, пустой stdin и ограниченные диагностики в памяти вместо неограниченного log-файла. Она не ограничена сроком пробы. `--native-probe-timeout` принимает целые секунды 1..4294967295, по умолчанию 10. Параметр хранится в CLI; обнаружение кандидатов, `--cc` и `--list-native-targets` реализуются в #211 и должны передать один срок всем вызовам `native_compiler_run` для кандидата. Общий сервис уборки и файловый слой реализованы в #245/#249; интеграция потребителей и inspect остаётся в #210/#211/#214/#230.

`scripts/check_child_process.py` запускает управляемые C-процессы с независимым внешним таймаутом. Проверяются успех/ошибка/авария, остановка, общий бюджет, потоки и границы, EOF/наследование ввода, quoting, отдельный результат, устойчивое ожидание родителя, потомки/внуки, выход корня до потомков, смерть владельца и независимые параллельные запуски. Готовность дерева подтверждают сами потомки через канал; паузы не задают порядок теста. Набор включён в Linux и все три Windows-конфигурации CI.

Дополнения для #245: `working_directory` меняет cwd только ребёнка;
`poll_child_process` возвращает CHILD_RUNNING без ожидания, но окончательное
подтверждение остановки дерева сохраняет прежний ограниченный срок.
`service_channel` и `CHILD_SERVICE_ARGUMENT` передают явно выбранный duplex
endpoint (владение исходным дескриптором остаётся у вызывающего).
`release_child_service` освобождает только объект `survive_parent`, не останавливая
его процесс; в Linux такой supervisor отсоединён двойным fork и не оставляет
клиенту обязанность ждать его перед выходом. Общий сервис описан в
[cleanup-service-api.md](cleanup-service-api.md).

## English

`src/lib/child_process.h` provides shell-free launch on Linux and Windows 10. A NULL executable launches the current executable. Paths and arguments are UTF-8; Windows uses `CreateProcessW` with CRT quoting. Start returns an owning object, wait pumps its streams, stop terminates its tree, and destroy stops unfinished work before releasing resources. Calls on one object must be serialized.

Create one absolute monotonic deadline with `child_deadline_after(ms)` **before** launch and reuse it for every command in a probe, including compiler identification, compilation and linking. Zero means unlimited. Output, protocol messages and phase changes never reset the deadline. Timeout is sticky. Cleanup has its own bounded period; `cleanup_error` means absence of surviving writers was not confirmed, so callers must retain affected files.

Output capture starts at launch, preserving the first 65536 arbitrary bytes of stdout and stderr separately. Exactly 65536 bytes is not truncation; further bytes set `truncated` and are drained without storage. Bounded read turns preserve deadline checks under continuous output. Captured prefixes survive crashes and timeouts; there is no ordering guarantee between streams. Capture errors are reported through `system_error` independently of process outcome.

An optional third bounded result pipe replaces the exact `CHILD_RESULT_ARGUMENT` argument with a decimal native handle. The child writes through `child_write_result` and closes it with `child_close_channel`. Output is never interpreted as protocol. JSON/Base64 and terminal-safe rendering belong to inspect (#214/#223).

Explicit `empty_stdin` opens a valid `/dev/null` or `NUL` handle before child code executes; reads return EOF without consuming the parent's input. Normal launches inherit stdin. `parent_watch` replaces `CHILD_PARENT_ARGUMENT` with a stable reference to the original parent: a Linux pidfd (kernel 5.3+) or Windows process handle. `child_wait_parent` is immune to PID reuse; unavailable mechanisms fail explicitly.

Linux creates a private subreaper before the worker. Owner socket closure triggers cleanup even during launch. The supervisor pins the root PID until group termination and kills/reaps only its own adopted children, including descendants that created new sessions. Enumeration requires `/proc/thread-self/children` in the matching PID namespace; failure is reported, never treated as successful cleanup. Windows assigns the process atomically to a non-breakaway Job Object during `CreateProcessW`, using `PROC_THREAD_ATTRIBUTE_JOB_LIST`. Ordinary jobs use kill-on-close; only explicitly selected standard/protocol handles are inherited. Remaining descendants are stopped after every root outcome, including success, before returning.

Explicit `survive_parent` supports the independent cleanup service in #245. Such a service waits on its stable parent reference and owns its work using ordinary child objects. On Windows this disables kill-on-close for the service itself: after owner death, the service is responsible for its work. This is ownership management, not a sandbox for hostile native code.

Existing DLL/SO compilation now uses the common API, EOF stdin and bounded in-memory diagnostics instead of an unlimited log file. User compilation remains unlimited. CLI `--native-probe-timeout` accepts integer seconds 1..4294967295, default 10. Candidate discovery, `--cc` and `--list-native-targets` belong to #211, which must pass one deadline to all `native_compiler_run` calls for a candidate. The shared cleanup service and filesystem layer are implemented in #245/#249; consumer and inspect integration remains in #210/#211/#214/#230.

`scripts/check_child_process.py` exercises controlled C fixtures with independent external watchdogs on Linux and all three Windows CI configurations. Coverage includes exit/failure/crash, stop, shared budgets, binary stream boundaries, EOF/inherited input, quoting, separate results, parent references, children/grandchildren, early root exit, owner death, launch races and independent concurrent trees. Descendant readiness is acknowledged over a pipe rather than inferred from sleeps.

Extensions for #245: `working_directory` changes only the child's cwd.
`poll_child_process` returns CHILD_RUNNING without waiting; final tree shutdown
still uses the existing bounded confirmation period. `service_channel` and
`CHILD_SERVICE_ARGUMENT` explicitly transfer a duplex endpoint while the caller
retains its original handle. `release_child_service` releases only a
`survive_parent` object, without stopping its process. On Linux its supervisor is
detached by double fork, so the client need not wait for it before exit. See
[cleanup-service-api.md](cleanup-service-api.md) for the shared service contract.
