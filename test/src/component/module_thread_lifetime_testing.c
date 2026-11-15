/** @file module_thread_lifetime_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Controlled entry/child/grandchild completion and late native lifetime.
 */
#define GOAT_NATIVE_OK GOAT_MODULE_NATIVE_OK
#include "common/module_compilation.h"
#undef GOAT_NATIVE_OK
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "model/builtin_function.h"
#include "model/native_library.h"
#include "model/thread.h"
#include "vm/vm.h"

/* Fixture actions and lifetime assertions must execute in Release too. */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>

static module_files_t *files;
static process_t *owner_process;
static thread_t *entry_thread, *second_thread;
static unsigned cleaned, completed;

static void cleanup(void *resource) {
    assert(resource == &cleaned);
    assert(completed == 2 && !owner_process->active_threads);
    ++cleaned;
}

static object_t *property(const gchar_t *name) {
    object_static_string_t storage = {0};
    return get_object_property(module_file_scope(module_files_main(files)),
                               get_static_string_object(&storage, name));
}

static operation_result_t spawn_join(thread_t *thread, object_t *function, bool native) {
    operation_result_t handle = spawn_program_thread(thread, function, NULL, 0);
    assert(!handle.is_exception);
    uint64_t id = (uint64_t)get_object_integer_value(handle.value).value;
    DECREF(handle.value);
    operation_result_t result = join_program_thread(thread, id);
    assert(!result.is_exception);
    if (native) {
        thread_t *worker = thread;
        while (worker->id != id)
            worker = worker->next;
        assert(worker->native_attempts == 1 && worker->native_successes == 1);
    }
    return result;
}

static operation_result_t grandchild(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    (void)count;
    while (!second_thread->finished)
        assert(wait_os_condition(thread->process->threads_changed, thread->process->execution_mutex)
               == OS_THREAD_OK);
    assert(entry_thread->finished && !cleaned);
    assert(get_object_integer_value(property(U"value")).value == 42);
    /* First library load and applicable native dispatch happen only AFTER both ancestors ended. */
    native_library_result_t loaded = load_native_library(LIFETIME_NATIVE_PATH);
    assert(loaded.status == NATIVE_LIBRARY_OK);
    set_native_library_cleanup(loaded.library, &cleaned, cleanup);
    native_function_descriptor_t *descriptor = create_native_function_descriptor(loaded.library, 1);
    assert(descriptor);
    object_t *function = create_function_object(thread->process,
                                                NULL,
                                                0,
                                                BAD_INSTR_INDEX,
                                                module_file_scope(module_files_main(files)),
                                                descriptor,
                                                false);
    static object_static_string_t native_key;
    assert(create_object_property(module_file_scope(module_files_main(files)),
                                  get_static_string_object(&native_key, U"lateNative"),
                                  function,
                                  true)
           == MSTAT_OK);
    release_native_function_descriptor(descriptor);
    destroy_native_library_result(&loaded);
    operation_result_t value = spawn_join(thread, function, true);
    assert(get_object_integer_value(value.value).value == 42 && !cleaned);
    DECREF(value.value);
    DECREF(function);
    ++completed;
    return operation_success(get_null_object());
}

static const builtin_function_t grandchild_descriptor = {.name = U"grandchild",
                                                         .execute = grandchild};
static builtin_function_object_t grandchild_object;

static operation_result_t second(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    (void)count;
    second_thread = thread;
    while (!entry_thread->finished)
        assert(wait_os_condition(thread->process->threads_changed, thread->process->execution_mutex)
               == OS_THREAD_OK);
    assert(!cleaned && module_file_state(module_files_main(files)) == MODULE_FILE_INITIALIZED);
    /* Calls a ready entry-file function that first initializes another source file. */
    operation_result_t value = spawn_join(thread, property(U"f"), false);
    assert(get_object_integer_value(value.value).value == 17);
    DECREF(value.value);
    operation_result_t handle = spawn_program_thread(
        thread,
        get_builtin_function_object(&grandchild_object, &grandchild_descriptor),
        NULL,
        0);
    assert(!handle.is_exception);
    DECREF(handle.value);
    ++completed;
    return operation_success(get_null_object());
}

static const builtin_function_t second_descriptor = {.name = U"second", .execute = second};
static builtin_function_object_t second_object;

static operation_result_t bootstrap(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    (void)count;
    assert(!thread->initialization_depth);
    entry_thread = thread;
    operation_result_t handle =
        spawn_program_thread(thread,
                             get_builtin_function_object(&second_object, &second_descriptor),
                             NULL,
                             0);
    assert(!handle.is_exception);
    DECREF(handle.value);
    return operation_success(get_null_object());
}

static const builtin_function_t bootstrap_descriptor = {.name = U"bootstrap", .execute = bootstrap};
static builtin_function_object_t bootstrap_object;

enum { MEMORY_ITERATIONS = 512, TEMP_ITEMS = 64 * 1024 };

static bool no_join_mode;
static size_t observed_samples, peak_working_delta, first_live, last_live, first_rss, last_rss;
static size_t last_records, last_os_handles, peak_os_handles;
static bool allow_join_target;
static thread_t *waiters[2];

static size_t current_rss_kib(void) {
#ifdef __linux__
    FILE *status = fopen("/proc/self/status", "r");
    if (!status)
        return 0;
    char line[256];
    size_t rss = 0;
    while (fgets(line, sizeof(line), status))
        if (sscanf(line, "VmRSS: %zu kB", &rss) == 1)
            break;
    fclose(status);
    return rss;
#else
    return 0; /* No portable resident byte counter; do not report a fake measurement. */
#endif
}

static uint64_t take_thread_id(operation_result_t handle) {
    assert(!handle.is_exception);
    uint64_t id = (uint64_t)get_object_integer_value(handle.value).value;
    DECREF(handle.value);
    return id;
}

static thread_t *worker_by_id(thread_t *caller, uint64_t id) {
    thread_t *worker = caller;
    do {
        if (worker->id == id)
            return worker;
        worker = worker->next;
    } while (worker != caller);
    assert(false);
    return NULL;
}

static operation_result_t temporary_worker(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    assert(!count);
    size_t before = get_allocated_memory_size();
    object_t **items = ALLOC(TEMP_ITEMS * sizeof(*items));
    object_t *leaf = create_integer_object(thread->process, 456789);
    for (size_t index = 0; index < TEMP_ITEMS; ++index)
        items[index] = leaf;
    object_t *array = create_array_object(thread->process, (object_array_t){items, TEMP_ITEMS});
    FREE(items);
    DECREF(leaf);
    static object_static_string_t storage;
    assert(create_object_property(thread->context->data,
                                  get_static_string_object(&storage, U"working"),
                                  array,
                                  true)
           == MSTAT_OK);
    push_object_onto_stack(thread->data_stack, array); /* Transfers its owned reference. */
    size_t delta = get_allocated_memory_size() - before;
    if (delta > peak_working_delta)
        peak_working_delta = delta;
    return operation_success(get_static_integer_object(7));
}

static operation_result_t sample_memory(object_t **args, uint16_t count, thread_t *thread) {
    assert(count == 1);
    thread_t *target = worker_by_id(thread, (uint64_t)get_object_integer_value(args[0]).value);
    if (no_join_mode) {
        while (!target->finished)
            assert(wait_os_condition(thread->process->threads_changed,
                                     thread->process->execution_mutex)
                   == OS_THREAD_OK);
    }
    assert(target->finished && !target->waiting_for && !target->context
           && target->data_stack == &target->completion_stack);
    assert(get_object_integer_value(target->completion_value).value == 7);
    size_t handles = 0, records = 0;
    for (thread_t *worker = thread->next; worker != thread; worker = worker->next) {
        if (worker->finished) {
            ++records;
            handles += worker->worker != NULL;
        }
    }
    /* In no-join mode the just-finished worker may be awaiting the next VM transition. */
    assert(handles <= 1);
    if (handles > peak_os_handles)
        peak_os_handles = handles;
    if (!observed_samples) {
        first_live = get_allocated_memory_size();
        first_rss = current_rss_kib();
    }
    ++observed_samples;
    last_live = get_allocated_memory_size();
    last_rss = current_rss_kib();
    last_records = records;
    last_os_handles = handles;
    return operation_success(get_null_object());
}

static operation_result_t no_join(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    (void)thread;
    assert(!count);
    return operation_success(get_boolean_object(no_join_mode));
}

static operation_result_t shared_target(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    assert(!count);
    while (!allow_join_target)
        assert(wait_os_condition(thread->process->threads_changed, thread->process->execution_mutex)
               == OS_THREAD_OK);
    object_t *value = create_integer_object(thread->process, 1234567);
    object_t *array = create_array_object(thread->process, (object_array_t){&value, 1});
    DECREF(value);
    return operation_success(array);
}

static operation_result_t joint_waiter(object_t **args, uint16_t count, thread_t *thread) {
    assert(count == 2);
    uint64_t target = (uint64_t)get_object_integer_value(args[0]).value;
    size_t slot = (size_t)get_object_integer_value(args[1]).value;
    assert(slot < 2 && !waiters[slot]);
    waiters[slot] = thread;
    broadcast_os_condition(thread->process->threads_changed);
    operation_result_t first = join_program_thread(thread, target);
    operation_result_t second = join_program_thread(thread, target);
    assert(!first.is_exception && !second.is_exception && first.value == second.value);
    assert(get_object_integer_value(get_array_elements(first.value).items[0]).value == 1234567);
    DECREF(second.value);
    return first;
}

static const builtin_function_t shared_descriptor = {.name = U"shared", .execute = shared_target};
static const builtin_function_t waiting_descriptor = {.name = U"waiting", .execute = joint_waiter};
static builtin_function_object_t shared_object, waiting_object;

static operation_result_t multi_join(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    assert(!count);
    uint64_t target = take_thread_id(
        spawn_program_thread(thread,
                             get_builtin_function_object(&shared_object, &shared_descriptor),
                             NULL,
                             0));
    uint64_t ids[2];
    object_t *target_id = create_integer_object(thread->process, (int64_t)target);
    for (size_t index = 0; index < 2; ++index) {
        object_t *arguments[] = {target_id, get_static_integer_object((int32_t)index)};
        ids[index] = take_thread_id(
            spawn_program_thread(thread,
                                 get_builtin_function_object(&waiting_object, &waiting_descriptor),
                                 arguments,
                                 2));
    }
    DECREF(target_id);
    thread_t *record = worker_by_id(thread, target);
    while (!waiters[0] || !waiters[1] || waiters[0]->waiting_for != record
           || waiters[1]->waiting_for != record)
        assert(wait_os_condition(thread->process->threads_changed, thread->process->execution_mutex)
               == OS_THREAD_OK);
    allow_join_target = true;
    broadcast_os_condition(thread->process->threads_changed);
    operation_result_t one = join_program_thread(thread, ids[0]);
    operation_result_t two = join_program_thread(thread, ids[1]);
    operation_result_t late = join_program_thread(thread, target);
    assert(!one.is_exception && !two.is_exception && !late.is_exception && one.value == two.value
           && two.value == late.value);
    DECREF(one.value);
    DECREF(two.value);
    DECREF(late.value);
    return operation_success(get_null_object());
}

static operation_result_t verify_failure(object_t **args, uint16_t count, thread_t *thread) {
    assert(count == 1);
    uint64_t id = (uint64_t)get_object_integer_value(args[0]).value;
    thread_t *worker = worker_by_id(thread, id);
    operation_result_t first = join_program_thread(thread, id);
    operation_result_t second = join_program_thread(thread, id);
    assert(first.is_exception && second.is_exception && first.value == second.value);
    assert(first.trace && first.trace == second.trace && first.trace == worker->exception.trace);
    assert(exception_trace_size(first.trace) > 0 && worker->failure_observed && !worker->context
           && !worker->completion_stack.size && !worker->completion_value);
    DECREF(first.value);
    DECREF(second.value);
    return operation_success(get_null_object());
}

static const builtin_function_t memory_builtins[] = {
    {.name = U"temporary", .execute = temporary_worker},
    {.name = U"sample", .execute = sample_memory},
    {.name = U"noJoin", .execute = no_join},
    {.name = U"multiJoin", .execute = multi_join},
    {.name = U"verifyFailure", .execute = verify_failure},
};
static builtin_function_object_t memory_objects[sizeof(memory_builtins) / sizeof(*memory_builtins)];
static object_static_string_t memory_keys[sizeof(memory_builtins) / sizeof(*memory_builtins)];

static void install_memory_builtins(object_t *scope) {
    for (size_t index = 0; index < sizeof(memory_builtins) / sizeof(*memory_builtins); ++index)
        assert(create_object_property(
                   scope,
                   get_static_string_object(&memory_keys[index], memory_builtins[index].name),
                   get_builtin_function_object(&memory_objects[index], &memory_builtins[index]),
                   true)
               == MSTAT_OK);
}

static void check_memory_results(process_t *process) {
    assert(observed_samples == MEMORY_ITERATIONS && last_records == MEMORY_ITERATIONS + 3);
    assert(peak_working_delta >= TEMP_ITEMS * sizeof(object_t *));
    assert(last_live <= first_live + MEMORY_ITERATIONS * sizeof(thread_t) + 256 * 1024);
    assert(!process->finished_workers);
    /* The object shared by the initial target and both waiters is reachable solely
       through their retained outcomes, including through final VM tracing. */
    thread_t *target = process->main_thread->next;
    object_t *shared = target->completion_value;
    assert(shared && waiters[0]->completion_value == shared
           && waiters[1]->completion_value == shared);
    assert(get_object_integer_value(get_array_elements(shared).items[0]).value == 1234567);
    for (thread_t *worker = process->main_thread->next; worker != process->main_thread;
         worker = worker->next)
        assert(!worker->context && worker->data_stack == &worker->completion_stack
               && !worker->worker);
    printf("mode=%s samples=%zu managed_first=%zu managed_last=%zu transient_peak=%zu "
           "records=%zu record_bytes_each=%zu os_handles_peak=%zu os_handles_last=%zu "
           "rss_first_kib=%zu rss_last_kib=%zu\n",
           no_join_mode ? "without-join" : "join",
           observed_samples,
           first_live,
           last_live,
           peak_working_delta,
           last_records,
           sizeof(thread_t),
           peak_os_handles,
           last_os_handles,
           first_rss,
           last_rss);
}

int main(int argc, char **argv) {
    fs_error_t argument_error = {0};
    assert(fs_arguments(&argc, &argv, &argument_error));
    assert(argc == 2 || argc == 3);
    bool memory_mode = argc == 3;
    no_join_mode = memory_mode && argv[2][0] == 'n';
    module_sources_t *sources = NULL;
    module_sources_error_t source_error = {0};
    module_compilation_t *compilation = NULL;
    module_compilation_error_t compile_error = {0};
    assert(!module_sources_read(argv[1], false, NULL, &sources, &source_error));
    assert(compile_module_sources(sources, &compilation, &compile_error));
    process_t *process = create_process();
    owner_process = process;
    assert(create_module_files_from_snapshot(process,
                                             compilation->metadata,
                                             compilation->code,
                                             &files));
    uint32_t main_file = module_file_id(module_files_main(files));
    assert(main_file < sources->count);
    /* Source discovery already resolved links and normalized platform separators.
       argv is UTF-8, but may still be relative or use Windows backslashes. */
    assert(prepare_process_launch(process,
                                  module_files_identity(files),
                                  main_file,
                                  sources->files[main_file].path));
    object_static_string_t key = {0};
    assert(create_object_property(
               module_file_scope(module_files_main(files)),
               get_static_string_object(&key, U"bootstrap"),
               get_builtin_function_object(&bootstrap_object, &bootstrap_descriptor),
               true)
           == MSTAT_OK);
    if (memory_mode)
        install_memory_builtins(module_file_scope(module_files_main(files)));
    int status = run_module_file(process, module_files_main(files));
    assert(!process->active_threads);
    if (memory_mode && argv[2][0] == 'u') {
        assert(status == 1 && process->main_thread->exception.value
               && process->main_thread->exception.trace);
        assert(process->main_thread->next->failure_observed
               && process->main_thread->next->exception.value
                      == process->main_thread->exception.value);
    } else if (memory_mode) {
        assert(status == 0);
        check_memory_results(process);
    } else {
        assert(status == 0 && completed == 2);
    }
    /* GC may release the unrooted descriptor at ordinary final cleanup, but never earlier. */
    destroy_process(process);
    assert(cleaned == (memory_mode ? 0 : 1));
    free_module_compilation(compilation);
    clear_module_compilation_error(&compile_error);
    free_module_sources(sources);
    clear_module_sources_error(&source_error);
    free_fs_arguments(argc, argv);
    assert(!get_allocated_memory_size());
    puts("OK");
    return 0;
}
