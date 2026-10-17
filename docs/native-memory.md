# Native host services / Сервисы интерпретатора для нативного кода

## Русский

ABI версии 2 передаёт каждому адаптеру `const goat_native_context_v2_t *`.
Контекст содержит указатель на неизменяемую виртуальную таблицу
`goat_native_services_v2_t` и непрозрачный указатель `state`. Нативный модуль не знает
устройства состояния и не содержит реализации аллокатора.

Сейчас таблица содержит только три метода памяти:

- `allocate(state, size)` выделяет неинициализированную память, выровненную для
  фундаментальных типов C. Нулевой размер резервирует отдельный живой блок.
  Нехватка памяти и переполнение размера завершают процесс; NULL не возвращается.
- `deallocate(state, pointer)` освобождает живой блок этого контекста. NULL допустим.
- `deallocate_all(state)` освобождает все оставшиеся блоки и оставляет контекст
  пригодным для повторного использования. Повторная очистка безопасна.

Публичные inline-обёртки `goat_native_allocate`, `goat_native_deallocate` и
`goat_native_deallocate_all` только вызывают методы таблицы. В них нет `malloc`,
`free`, `ALLOC`, устройства списков или другой реализации управления памятью.
Пример внутри нативной функции:

```c
void *temporary = goat_native_allocate(context, 1024);
/* Использование temporary. */
goat_native_deallocate(context, temporary);
```

Реализация находится исключительно в интерпретаторе:
`src/model/native_services.c`. Методы используют штатные `ALLOC/FREE`, поэтому
нативные выделения учитываются в `get_allocated_memory_size`, попадают в отладочный
список и проверяются на повреждение защитных байтов. Заголовок списка и полезная
память составляют одно штатное выделение. Удаление блока занимает O(1), очистка O(n).

Для внешнего вызова VM создаёт пустой `native_memory_t` на стеке и связывает его с
таблицей через `init_native_context`. Инициализация не выделяет память. Все вложенные
функции сгенерированного модуля получают тот же контекст скрытым аргументом. После
сохранения результата в VM владелец вызывает полную очистку. Она выполняется также
при ошибке, неверном результате и выходе через ограничитель стека — до повтора на VM.

Полную очистку выполняет только владелец внешнего вызова. Вложенные функции могут
освобождать свои временные блоки, но не все блоки контекста. Указатели и контекст нельзя
сохранять после возврата. Освобождение чужого блока, двойное освобождение и копирование
состояния с живыми блоками нарушают контракт. Повышенное выравнивание сверх
фундаментального в этот контракт не входит.

Таблица общая и неизменяемая; состояние независимо для каждого внешнего вызова,
включая повторный вход. Разные контексты могут работать одновременно в разных потоках.
Один контекст используется одним потоком одновременно. Учёт памяти и отладочный
список штатного аллокатора синхронизированы, в том числе при смешивании обычных
`ALLOC/FREE` и вызовов через нативные сервисы.

`struct_size` описывает размер таблицы. Адаптер проверяет необходимый префикс методов
памяти, а не размер всей потенциально расширенной таблицы. Будущие сервисы, например
печать, можно добавлять в конец: их публичные обёртки также будут перенаправлять вызов
в интерпретатор. Печать и другие сервисы этим изменением не добавлены.

Экспорт — `goat_native_query_v2`; DLL/SO версии 1 нужно пересобрать. Передаваемые
значения пока числовые: интерфейс сервисов не зависит от строк. Независимый хост
может предоставить собственную таблицу, соблюдающую ABI; реализация всё равно
остаётся у хоста, а не в нативном модуле.

Тесты проверяют штатный счётчик и отладочный отчёт, повреждение защитного байта,
выравнивание, ленивость, освобождение отдельных блоков и всех остатков, повторное
использование, четыре потока, рекурсию и выход через ограничитель стека. Числовые
тестовые хосты передают таблицу-заглушку, которая аварийно завершает тест при любой
неожиданной аллокации: числовой код не должен требовать выделения памяти.

## English

ABI v2 passes a `const goat_native_context_v2_t *` to every adapter. It contains an
immutable `goat_native_services_v2_t` vtable and opaque per-invocation state. The native
module knows neither the state layout nor the allocation implementation.

The table currently exposes allocate, deallocate and deallocate_all. Public inline
wrappers only dispatch through these methods. They contain no malloc/free, ALLOC/FREE,
allocation lists or allocator implementation. Future services such as printing may be
appended to the table; consumers validate the required memory prefix using struct_size.
No printing service is implemented yet.

All allocation implementation resides in `src/model/native_services.c`, in the
interpreter. Its callbacks use standard ALLOC/FREE, preserving memory accounting,
debug allocation reports and trailer checks. One tracked allocation contains both
list header and payload. Individual release is O(1), bulk release O(n).

The VM binds a zero-initialized stack state to the shared immutable table, without heap
initialization. Nested generated functions borrow that same context. The outer owner
bulk-clears after consuming results, on success or failure, including stack-limit
recovery and before VM fallback. Callees may individually free owned temporaries, but
must not bulk-clear the caller's state or retain pointers beyond the outer call.

Allocation never returns NULL: OOM and size overflow are fatal. Zero size reserves a
distinct block. Memory is uninitialized and fundamentally aligned, including MinGW i386
extended scalars; explicitly over-aligned types are outside this contract. Freeing NULL
is harmless. Other pointers must identify live blocks from this context. Bulk cleanup
is repeatable; never copy live state. Independent contexts, including reentrant outer
calls, have separate state and can run concurrently. One context is thread-confined.
Standard allocator accounting and its debug list are synchronized across threads.

The query export is goat_native_query_v2; v1 DLL/SO files must be rebuilt. Values remain
numeric and the services are independent of strings. Standalone hosts supply their own
vtable; implementation stays in the host. Tests cover interpreter accounting, debug
reports and trailer checks, laziness, alignment, lifetime, concurrent use, recursion,
and stack-limit recovery. Numeric fixtures reject unexpected allocation requests.
