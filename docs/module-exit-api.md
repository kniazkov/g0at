# Top-level return and exit / Верхнеуровневый return и exit (#207)

## Contract / Контракт

- `return` outside a function throws `Exceptions.RETURN_OUTSIDE_FUNCTION` at
  runtime, before searching for return cleanup. It can be caught in the same
  file; an escaping initializer error is cached by the existing file gate.
  Return expressions are evaluated normally before the return instruction.
- `exit(code)` accepts exactly one integer in the inclusive range 0–255. No
  coercion, truncation or modulo operation is performed. Invalid calls throw
  `Exceptions.INVALID_ARGUMENT` before choosing a termination status.
- `thread_t.initialization_depth` is the dynamic include/import execution
  context. Synchronous file entry increments it and restores it on every
  outcome; function calls, aliases and callbacks share the same thread context.
  Only the outer entry matching the frozen launch module/file is exempt. A valid
  exit during initialization throws `Exceptions.EXIT_DURING_INITIALIZATION`.
- Normal exit records `process_t.exit_requested` and `exit_code`. The VM stops
  before another instruction, without exception dispatch or user finally.
  Re-entry cannot resume a terminated process. `run` and `run_module_file`
  return the selected status, including zero; hosts distinguish explicit exit
  from ordinary completion using the flag. Status is not a saved file failure.
- The CLI preserves all 256 statuses, and interactive execution stops reading
  commands after exit. Native-report write failure cannot replace an explicitly
  selected status. Existing host destruction and C-stream finalization still
  occur; this does not execute user code.

Верхнеуровневый return — перехватываемое исключение, а не синтаксическая ошибка
и не успешное завершение файла. Проверка exit строгая и выполняется до выбора
кода выхода. Динамический контекст подключаемой инициализации сохраняется через
вызовы функций и обратные вызовы; прямой запуск главного файла из него исключён.
Штатный exit останавливает всю VM без пользовательских catch/finally, сохраняет
код 0–255 и не позволяет продолжить исполнение при повторном входе хоста.

## Boundaries / Границы задач

This is the #207 language/runtime layer, reusing the existing lazy initialization
and exception machinery. It does not introduce a second loader, initializer or
cleanup service. #217 adds thread initialization serialization using the common
dynamic context. #230 implements atomic cross-thread termination, the shared
one-second flush budget and full native/cleanup-service shutdown integration.
The current single-thread termination flag is not advertised as an atomic
multi-thread shutdown protocol. Full `.gmod` CLI integration remains #191;
compiled graph tests use the existing verified snapshot execution harness.

Полная процедура завершения с потоками, native и уборщиком — отдельная #230;
ограничение времени сброса вывода здесь не заявляется реализованным. Проверки
скомпилированных графов используют существующий загрузчик проверенного снимка,
а не подменяют будущую CLI-интеграцию #191.

## Tests / Проверки

`scripts/check_module_exit.py build/module_import_execution_testing build/goat`
checks direct source and compiled graphs, exact exit statuses, invalid calls,
catch/finally, indirect calls, callbacks into the launch module, included/imported
initializers, sticky failures and the same archive launched versus imported.
The existing functional suite adds top-level return cases for exception ordering,
return expression effects, loops and uncaught errors. CI runs the module suite
on Linux and all supported Windows toolchains. Sanitizer checks run the same
execution paths without weakening CI requirements.
