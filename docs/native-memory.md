# Native memory / Память нативных вызовов

## Русский

ABI версии 2 добавляет обязательный параметр `const goat_native_allocator_v2_t *`
между номером версии и количеством аргументов адаптера. Экспорт называется
`goat_native_query_v2`; библиотеки версии 1 нужно пересобрать. Представление
передаваемых значений пока остаётся числовым: этот контракт не зависит от строк.

Аллокатор содержит размер структуры, непрозрачный указатель `context` и три callback-функции:

- `allocate(context, size)` возвращает ненулевой указатель. Нехватка памяти или
  переполнение размера завершают процесс. Нулевой размер выделяет отдельный живой блок.
- `deallocate(context, pointer)` освобождает один блок. `NULL` допустим.
- `deallocate_all(context)` освобождает оставшиеся блоки. Повторный вызов безопасен;
  после очистки аллокатор можно использовать снова.

Возвращённая память не инициализирована; выравнивание достаточно для фундаментальных
типов C, включая расширенные скалярные типы MinGW i386. Явно повышенное выравнивание
не входит в этот контракт. Освобождать можно только начало живого блока своего
аллокатора. Повторное освобождение и освобождение чужого блока нарушают контракт.

VM создаёт на стеке пустое состояние перед каждым внешним вызовом адаптера.
Инициализация не обращается к куче. Список выделений появляется при первой аллокации;
служебные данные находятся в том же выделении, что и полезная память. Удаление одного
блока занимает O(1), очистка всех — O(n). Вложенные функции сгенерированного модуля
получают тот же аллокатор скрытым параметром и не очищают его при возврате.

Владелец контекста сначала переносит результат в VM, затем вызывает `deallocate_all`.
При ошибке, неверном результате или возврате из ограничителя стека очистка также
обязательна и происходит до возможного повторного выполнения на VM. Нативная функция
может досрочно освобождать свои временные блоки. Общую очистку выполняет только
владелец внешнего вызова: иначе могут исчезнуть данные вызывающей функции.
Нативный код не должен сохранять контекст или его память за пределами вызова.

Для самостоятельных хостов доступна необязательная реализация
`include/goat/native_allocator.h`:

```c
goat_native_allocator_state_t state = {0};
goat_native_allocator_v2_t allocator = goat_native_allocator_init(&state);
uint32_t status = entry->invoke(
    GOAT_NATIVE_ABI_VERSION, &allocator, count, arguments, &result);
/* Здесь обработать статус и сохранить результат, пока память ещё жива. */
allocator.deallocate_all(allocator.context);
```

Реализация находится на стороне хоста. DLL использует переданные callback-функции,
поэтому выделение и освобождение выполняются одним CRT. Вместо стандартной реализации
можно передать свою с тем же контрактом. `context` может быть NULL, если это допускает
пользовательская реализация; сами callback-функции обязательны.

Независимые экземпляры не имеют общего изменяемого состояния и могут работать
одновременно в разных потоках. Один экземпляр используется только одним потоком
одновременно. Копировать состояние с живыми блоками нельзя. Повторный вход из хоста
создаёт отдельный контекст; TLS и глобальный текущий аллокатор не используются.

Проверки входят в `scripts/check_native_abi.sh`: ленивость, нулевой размер,
выравнивание, досрочное и полное освобождение, повторное использование, четыре потока,
фатальная нехватка памяти и переполнение размера. Инструментированный сгенерированный
модуль проверяет передачу контекста через рекурсию и очистку после ограничителя стека.

## English

ABI v2 adds a mandatory `const goat_native_allocator_v2_t *` parameter between the
adapter's version and argument count. The export is `goat_native_query_v2`; v1 libraries
must be rebuilt. Values remain numeric. The allocation service is independent of strings.

The descriptor contains its size, an opaque context and three host callbacks:
`allocate`, `deallocate`, and `deallocate_all`. Allocation never returns NULL; OOM and
size overflow terminate the process. Zero size allocates a distinct live block.
Memory is uninitialized and supports fundamental C alignment (including MinGW i386
extended scalars), but not explicitly over-aligned types. Freeing NULL is harmless;
other pointers must identify live blocks from this instance. Bulk cleanup is repeatable
and leaves the instance reusable.

Each outer VM-to-native call owns a zero-initialized stack state. Initialization performs
no heap allocation. Each block carries an intrusive list header: individual release is
O(1), bulk release O(n). Nested generated module functions borrow the same allocator.
Only the outer owner bulk-clears it, after consuming results, on both success and failure,
including stack-limit recovery and before VM retry. Callees may individually release
owned temporaries, but must not retain pointers or the service beyond the call.

`include/goat/native_allocator.h` provides an optional header-only host implementation.
The sample above also applies to custom hosts. DLLs call host callbacks, so one CRT owns
both allocation and release. Custom implementations may use a NULL context; all three
callbacks are required. Independent instances have no shared mutable state and can run
concurrently. An instance is thread-confined; never copy live state. Reentrant outer calls
use separate contexts. There is no global or TLS current allocator.

The ABI script covers allocation lifetime, laziness, alignment, four concurrent threads,
fatal OOM/overflow, recursive context propagation and cleanup after stack-limit recovery.
