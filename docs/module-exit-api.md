# Process exit and cleanup / Завершение процесса и очистка (#207, #230)

## Language boundary / Граница языка

- Top-level `return` throws `Exceptions.RETURN_OUTSIDE_FUNCTION`, including in
  the directly launched file. It is an ordinary exception, not successful file
  completion or process exit. Its expression is evaluated before the check.
- `exit(code)` accepts exactly one integer in `0..255`, without coercion,
  truncation or modulo arithmetic. Invalid arity, type or range throws
  `Exceptions.INVALID_ARGUMENT` before reserving an exit code.
- `thread_t.initialization_depth` describes dynamic include/import initialization,
  including indirect calls and native host callbacks. A valid exit in that
  context throws `Exceptions.EXIT_DURING_INITIALIZATION`; ordinary catch/finally
  and cached initialization failures still apply. The same ready library
  function called outside initialization can terminate the whole process.
- The directly launched entry file is ordinary execution, not an imported
  initializer. Its completion leaves other program threads alive; descendants
  may create more threads and make their first native calls. Snapshots, ready
  entry declarations and loaded libraries outlive every program thread.

## Explicit termination / Явное завершение

`request_program_exit` performs the shared language/context checks, then calls
`process_exit_request` (`lib/process/process_exit.h`), which never returns. Its
`process_exit_select`/`process_exit_selected` arbiter uses one atomic encoded
state/status, independently testable without terminating a process.

After successful language checks, one atomic transition selects the process exit
code. Its winner owns termination; other callers cannot overwrite the code,
restart cleanup, extend the deadline or return to user code. Both zero and 255
are ordinary valid statuses; a race between zero and seven allows either, never
a third result. Rejected calls do not reserve a status.

A valid exit never returns through `run`, `run_module_file` or their host. It does
not unwind user catch/finally, wait for worker/native completion, free VM objects,
unload DLL/SO, invoke C `atexit` handlers or write a deferred native report.
Nonreturning native loops and blocked native calls cannot retain the process.
Already performed effects are not rolled back or replayed on the VM.

`process_t.exit_requested` and `exit_code` remain a separate host-cancellation
mechanism used by embedding/testing code. They are not the atomic arbiter for a
language exit and do not provide its OS-wide termination guarantee.

После проверки языка единственный атомарный переход фиксирует код завершения.
Повторные вызовы не возвращаются в пользовательский код, не меняют код и не
запускают завершение повторно. Ошибочный вызов и вызов из инициализатора остаются
обычными исключениями и не резервируют код. Допустимый exit завершает весь
процесс, не дожидаясь других потоков или возврата из native. Он не возвращается
к хосту: нет последующей записи native-отчёта, пользовательских finally,
деструкторов VM или ручной выгрузки библиотек из-под работающего кода.

## Output deadline / Срок сброса вывода

The winner attempts to flush stdout and stderr with **one common one-second
monotonic budget**. Lock acquisition, writes and both streams share that budget.
An independent watchdog is prearmed by `process_exit_prepare` during process
creation, with bounded readiness before native code can hold the Windows loader
lock. Exit creates no
thread. The watchdog enforces the deadline even if the flushing thread is blocked
inside the C library or OS. Early completion terminates immediately, without waiting out the second. Errors or incomplete output retain
the selected code; no potentially blocking warning is emitted to stderr. If
watchdog preparation failed or was omitted, exit immediately terminates with the
selected code rather than risking an unbounded flush. POSIX flushing blocks
SIGPIPE on that thread so a closed output pipe cannot replace the selected status.

Сброс stdout и stderr ограничен одной общей секундой монотонного времени,
включая ожидание блокировок и запись. Сторожевой поток заранее запускается при
создании процесса, до входа в native; в самом exit поток не создаётся. Он прерывает процесс даже при зависшем сбросе.
Если подготовка сторожевого потока не удалась, exit завершается сразу с выбранным
кодом без попытки потенциально неограниченного сброса. При быстром сбросе остаток
секунды не выжидается. Потеря части вывода при таймауте допустима; ошибка или таймаут не
меняет выбранный код. Дополнительное предупреждение в заблокированный stderr
не записывается.

## Native boundary / Нативная граница

The host service tables provide an optional size-checked `request_exit` tail.
Existing mandatory prefixes and value layouts stay unchanged. The service
retains exact argument count/type/range and the owning thread's dynamic
initializer context. Valid exit does not return. A rejected request returns the
host-exception status, and the adapter must immediately propagate it without
further effects; the host raises the original Goat exception, never retries on
VM. Exit-containing code that is not safely supported by code generation remains
on the VM; generated code must never substitute a private C/OS exit.

Нативный ABI использует дополнительный хвост таблицы сервисов `request_exit`,
наличие которого проверяется по размеру. Обязательный префикс и формат значений
не меняются. Проверки аргументов и контекста выполняет общий механизм хоста.
Отказ возвращает статус исключения хоста: адаптер сразу передаёт его наружу,
не продолжает эффекты и не запускается повторно на VM. Неподдерживаемый генератором
вызов остаётся на VM, а не заменяется самостоятельным C exit.

## One cleanup owner / Один уборщик

Reuse `cleanup_service_t` from #245 for both temporary `.goat` compilation and
`.gbin` extraction. Readiness is confirmed before any temporary compiler/file or
library loading operation. Startup failure is sticky: auto warns once and uses
VM globally; required fails terminally. Its separate one-second readiness budget
is not the output-flush budget.

The independent cleaner already observes the actual owner process identity.
Neither normal completion nor exit sends it a second shutdown request or waits
for it. Only after owner death does it stop any owned compiler descendants and
remove temporary files. On explicit exit the OS owns loaded-library release;
normal host destruction occurs only after all program threads finish. Saved
`.gbin`/libraries are not cleanup-owned. Cleaner failure never changes the
selected exit code; after a confirmed cleaner dies, the program continues and
removal is no longer guaranteed. No compensating deletion under running code is
allowed.

Используется только общий уборщик #245. Он наблюдает за фактической смертью
процесса Goat и затем удаляет временные файлы; до смерти владельца каталог
остаётся доступен. Exit не запускает второго уборщика, не передаёт повторное
задание и не ждёт его завершения. Сохранённые пользователем артефакты не удаляются.
Ошибка уборки не меняет код выхода; гибель уже готового уборщика не останавливает
Goat и не разрешает удаление файлов из-под продолжающегося исполнения.

## Verification / Проверки

- `check_module_exit.py` exercises strict arguments, direct source, `.gmod`, ZIP
  `.gbin`, imported/ included files, indirect calls, callbacks, cached failures,
  catch/finally and top-level return through both runtime and public CLI paths.
- `check_process_exit.py build/process_exit_testing build/process_exit_failure_testing
  build/process_exit_delayed_testing` exercises the atomic winner, fast unbuffered
  and buffered termination, blocked output/stream locks, one common deadline,
  racing requests and unchanged codes after output failure.
- `check_native_exit.py` covers rejected calls across the ABI boundary and
  process termination while another native worker cannot return.
- `check_module_loading.py` retains first-call serialization, snapshot lifetime,
  late worker loading, pre-READY failure and death of an already ready cleaner.
  Real loaded DLL/SO adapters loop forever or block without cooperation; an
  explicit marker proves entry before VM exit, and cleanup is awaited separately.
- `check_native_cleanup.py` verifies direct source and actual CLI module
  descriptor/archive execution in auto/required modes, ordinary/explicit exit,
  no deletion while the owner is alive, retained saved artifacts and interrupted
  compiler descendants. It waits for cleaner process completion or its explicit
  completion marker, not an arbitrary sleep after Goat exits.

All concurrent scenarios use explicit handshakes; external timeouts detect a
hang, not prove ordering. Deadline assertions allow scheduler delays. Existing
Linux/Windows build scripts run the suites without CTest. Local unsupported ACL
or process-inspection limitations do not weaken CI checks.
