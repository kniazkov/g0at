# Потоки C / C threads

## Русский

`lib/threads/os_thread.h` предоставляет отдельный API потоков, нерекурсивных мьютексов и условных переменных. Типы непрозрачны; заголовок не экспортирует pthread/Windows-типы. Это системные потоки C, не `model/thread.h`, не процессы и не пул потоков.

`create_os_thread(&thread, callback, argument, stack_size)` запускает callback сразу; его сигнатура — `void *callback(void *)`. Ноль в размере стека выбирает системное значение; ненулевой размер запрашивает стек в байтах (платформа может округлять его и отклонять неподдерживаемые размеры). Windows использует `_beginthreadex` с резервированием стека, Linux — pthread attributes. Callback возвращается обычным `return`; отмена потока, detach и сторонние функции завершения потока не поддерживаются.

`join_os_thread(thread, &result)` ждёт завершения и возвращает указатель, полученный от callback; `result` может быть NULL. Один успешный join обязателен перед `destroy_os_thread`. Повторный join даёт `OS_THREAD_INVALID`, join самого себя — `OS_THREAD_DEADLOCK`, уничтожение ещё не присоединённого потока — `OS_THREAD_BUSY`, даже если callback уже закончился. Вызовы над одним thread handle сериализует вызывающий код. Данные аргумента должны жить до окончания callback; данные результата — до окончания их использования.

`create_os_mutex`, `lock_os_mutex`, `try_lock_os_mutex`, `unlock_os_mutex`, `destroy_os_mutex` реализуют нерекурсивный мьютекс. `try_lock` возвращает `OS_THREAD_BUSY`, когда мьютекс занят другим потоком; обе операции захвата возвращают `OS_THREAD_DEADLOCK` при рекурсивном захвате. Разблокировать может только владелец: чужой или повторный unlock возвращает `OS_THREAD_INVALID`. Поток обязан освобождать свои блокировки перед завершением. Это не robust mutex для восстановления после аварийного завершения владельца.

Условие создаётся через `create_os_condition`. `wait_os_condition(condition, mutex)` требует владения мьютексом, атомарно отпускает его на время ожидания и захватывает снова перед возвратом. Предикат всегда проверяется в цикле под тем же мьютексом: возможны ложные пробуждения. Для одновременно ожидающих на одном условии используется один мьютекс. `signal_os_condition` будит одного ожидающего, `broadcast_os_condition` — всех; уведомления не накапливаются. Изменение предиката и уведомление обычно выполняются под защищающим его мьютексом.

```c
lock_os_mutex(mutex);
while (!ready)
    wait_os_condition(condition, mutex);
/* Здесь mutex снова захвачен. */
unlock_os_mutex(mutex);
```

Уничтожение мьютекса/условия требует исключить новых и параллельных пользователей. Уже заблокированный мьютекс, активное ожидание условия или мьютекс, отпущенный для такого ожидания, дают `OS_THREAD_BUSY`. Проверка BUSY не заменяет управление временем жизни: нельзя освобождать объект одновременно с попыткой другого потока начать операцию. NULL допустим у destroy; остальные отсутствующие обязательные аргументы дают INVALID. Создание обнуляет выходной указатель при ошибке. `OS_THREAD_ERROR` обозначает сбой платформенной операции; нехватка памяти следует общей политике ALLOC проекта.

Linux использует pthread mutex/condition; Windows — SRWLOCK/CONDITION_VARIABLE и системные thread handles. Владение мьютексом проверяется общим кодом; учёт владельца и ожидающих атомарный. Платформенная зависимость потоков подключается через CMake `Threads::Threads` на POSIX, Windows-реализация не требует winpthreads.

Проверки включены в unit-набор и запускаются в отдельном процессе с внешним сроком 10 секунд, чтобы ошибка синхронизации не подвесила весь стенд. Проверяются жизненный цикл, результат callback, малый стек, self/repeated join, NULL, занятые объекты, чужой unlock, конкурирующие счётчики, signal и broadcast. Готовность подтверждается предикатами под мьютексом, а не паузами. Существующие native-тесты конкурентных адаптеров и малого стека также используют этот API.

Задача #216 не меняет время жизни VM: все её потоки равнозначны, завершение потока входного файла не останавливает остальные. Реализация этого правила относится к #217, завершение программы через exit — к #230.

## English

`lib/threads/os_thread.h` is an independent C API for OS threads, nonrecursive mutexes and condition variables. Opaque types expose no platform headers. It introduces neither a thread pool nor a VM lifetime policy.

`create_os_thread(&thread, callback, argument, stack_size)` immediately starts a `void *callback(void *)`. Zero selects the default stack; a nonzero byte count requests a stack size subject to platform rounding/validation. Windows uses `_beginthreadex` with stack reservation; Linux uses pthread attributes. Callbacks return normally. Cancellation, detach and external thread-exit functions are unsupported.

`join_os_thread(thread, &result)` waits and returns the callback's pointer; the output argument is optional. Join exactly once before destruction. Repeated join returns `OS_THREAD_INVALID`; self-join returns `OS_THREAD_DEADLOCK`; destroying an unjoined handle returns `OS_THREAD_BUSY`, even after the callback finishes. Serialize operations on the same thread handle. Keep callback arguments and returned data alive while used.

Mutex lock and try-lock reject recursive acquisition with DEADLOCK. Try-lock returns BUSY for another owner's lock. Only the owner may unlock; foreign and repeated unlock return INVALID. A thread must release its locks before returning; mutexes do not recover from owner death.

Condition wait requires an owned mutex, atomically releases it and returns with it reacquired. Always recheck the predicate in a loop under that mutex: wakeups can be spurious. Concurrent waits on a condition must use the same mutex. Signal wakes one waiter; broadcast wakes all. Notifications are not stored. Normally change the predicate and notify while holding its mutex.

Destroy mutex/condition only after excluding new and concurrent users. Locked mutexes and active condition waits return BUSY; the mutex temporarily released by a condition wait also remains busy for destruction. BUSY checks do not make concurrent destruction safe. Destroy(NULL) succeeds; missing required arguments return INVALID. Creation clears the output pointer on failure. ERROR denotes a platform failure; allocation follows the project's fatal ALLOC policy.

Linux uses pthread primitives and CMake `Threads::Threads`; Windows uses native thread handles, SRWLOCK and CONDITION_VARIABLE without winpthreads. Shared atomic bookkeeping verifies ownership and tracks waiters. Existing native adapter concurrency and small-stack tests use this layer too.

The unit suite runs lifecycle/result, small-stack, invalid/self/repeated join, NULL, busy-object, foreign-unlock, contended-counter, signal and broadcast tests inside a child process with a 10-second external deadline. Ordering uses mutex-protected predicates and acknowledgements, never sleeps.

All VM threads remain equal: ending the entry-file thread must not stop the others. That VM policy belongs to #217; explicit whole-program exit belongs to #230.
