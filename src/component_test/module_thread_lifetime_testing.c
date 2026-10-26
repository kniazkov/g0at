/** @file module_thread_lifetime_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Controlled entry/child/grandchild completion and late native lifetime.
 */
#define GOAT_NATIVE_OK GOAT_MODULE_NATIVE_OK
#include "common/module_compilation.h"
#undef GOAT_NATIVE_OK
#include "lib/allocate.h"
#include "lib/file_system.h"
#include "model/builtin_function.h"
#include "model/native_library.h"
#include "model/thread.h"
#include "vm/vm.h"

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

int main(int argc, char **argv) {
    fs_error_t argument_error = {0};
    assert(fs_arguments(&argc, &argv, &argument_error));
    assert(argc == 2);
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
    assert(run_module_file(process, module_files_main(files)) == 0);
    assert(completed == 2 && !process->active_threads);
    /* GC may release the unrooted descriptor at ordinary final cleanup, but never earlier. */
    destroy_process(process);
    assert(cleaned == 1);
    free_module_compilation(compilation);
    clear_module_compilation_error(&compile_error);
    free_module_sources(sources);
    clear_module_sources_error(&source_error);
    free_fs_arguments(argc, argv);
    assert(!get_allocated_memory_size());
    puts("OK");
    return 0;
}
