/** @file module_initialization_contract_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Blocking initialization and stable saved exceptions across callers.
 */
#include "lib/allocate.h"
#include "lib/file_system.h"
#include "lib/wire.h"
#include "model/module_files.h"
#include "model/thread.h"
#include "vm/gc.h"
#include "vm/vm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

typedef struct {
    unsigned calls;
} test_t;

static object_t *key(const gchar_t *text, object_static_string_t *storage) {
    return get_static_string_object(storage, text);
}

static bool execute(module_file_t *file, context_t *frame, void *data, exception_t *failure) {
    test_t *test = data;
    assert(++test->calls == 1);
    assert(module_file_state(file) == MODULE_FILE_INITIALIZING);
    process_t *process = module_file_process(file);
    assert(process->initializing_file == file);
    puts("READY");
    assert(!fflush(stdout));
    /* No deadline belongs to this callback or to the production initialization gate.
     * The Python test owns the external watchdog and eventually releases stdin. */
    assert(getchar() == 'R');
    assert(module_file_state(file) == MODULE_FILE_INITIALIZING);
    assert(process->initializing_file == file);
    thread_t *thread = process->main_thread;
    context_t *previous = thread->context;
    instr_index_t address = thread->instr_id;
    size_t before = thread->data_stack->size;
    thread->context = frame;
    thread->instr_id = module_file_entry(file);
    int status = run(process, module_file_bytecode(file));
    assert(thread->context == frame && thread->data_stack->size == before);
    thread->context = previous;
    thread->instr_id = address;
    if (status) {
        assert(thread->exception.value && thread->exception.trace);
        *failure = thread->exception;
        thread->exception = (exception_t){0};
    }
    return !status;
}

int main(int argc, char **argv) {
    assert(argc == 2);
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    unsigned char sizes[8];
    assert(FREAD(sizes, 1, sizeof(sizes), stdin) == sizeof(sizes));
    size_t metadata_size = read_le32(sizes), image_size = read_le32(sizes + 4);
    assert(metadata_size && metadata_size < 1024 * 1024);
    assert(image_size && image_size < 1024 * 1024);
    char *metadata = malloc(metadata_size);
    unsigned char *image = malloc(image_size);
    assert(metadata && image);
    assert(FREAD(metadata, 1, metadata_size, stdin) == metadata_size);
    assert(FREAD(image, 1, image_size, stdin) == image_size);
    process_t *process = create_process();
    module_files_t *files = NULL, *other = NULL;
    assert(create_module_files(process, metadata, metadata_size, image, image_size, &files, NULL)
           == MODULE_BYTECODE_OK);
    assert(create_module_files(process, metadata, metadata_size, image, image_size, &other, NULL)
           == MODULE_BYTECODE_OK);
    free(metadata);
    module_file_t *file = module_files_get(files, 0);
    object_t *scope = module_file_scope(file);
    object_t *root = get_root_object();
    bool success = !strcmp(argv[1], "success"), object = !strcmp(argv[1], "object");
    object_t *payload = !strcmp(argv[1], "null") ? get_null_object()
                        : object ? create_user_defined_object(process, (object_array_t){&root, 1})
                                 : create_integer_object(process, 7001);
    object_static_string_t ps = {0}, cs = {0}, vs = {0};
    object_t *payload_key = key(U"payload", &ps), *counter_key = key(U"counter", &cs);
    object_t *value_key = key(U"value", &vs);
    assert(create_object_property(scope, payload_key, payload, false) == MSTAT_OK);
    assert(create_object_property(scope, counter_key, get_static_integer_object(0), false)
           == MSTAT_OK);
    DECREF(payload);
    test_t test = {0};
    context_t *caller = process->main_thread->context;
    object_t *result = NULL;
    exception_t failure = {0};
    module_initialize_status_t status =
        initialize_module_file(file, caller, execute, &test, &result, &failure);
    assert(status == (success ? MODULE_INITIALIZE_OK : MODULE_INITIALIZE_FAILED));
    assert(module_file_state(file) == (success ? MODULE_FILE_INITIALIZED : MODULE_FILE_FAILED));
    assert(!process->initializing_file && process->main_thread->context == caller);
    assert(success ? result == scope && !failure.value && !failure.trace
                   : !result && failure.value == payload && failure.trace);
    /* Borrow only: the failed file, not an external exception record, must keep
     * both members alive through each collection below. */
    exception_t original = module_file_exception(file);
    const exception_trace_frame_t *origin = exception_trace_frame(original.trace, 0);
    exception_trace_frame_t origin_copy = {0};
    if (!success) {
        assert(origin && origin->module == module_files_identity(files) && origin->file_id == 0);
        assert(origin->instruction == module_file_entry(file) + 5);
        origin_copy = *origin;
    }
    clear_exception(&failure);
    if (object)
        assert(create_object_property(payload, value_key, get_static_integer_object(23), false)
               == MSTAT_OK);
    if (!success)
        assert(set_object_property(scope, payload_key, get_null_object()) == MSTAT_OK);
    collect_garbage(process);
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        /* A caller from another file and another module must not replace the origin
         * trace or produce another initializer invocation. */
        module_files_t *owner = attempt % 2 ? other : files;
        context_t *next = create_module_file_context(module_files_get(owner, 1), caller);
        assert(next);
        status = initialize_module_file(file, next, execute, &test, &result, &failure);
        assert(status == (success ? MODULE_INITIALIZE_OK : MODULE_INITIALIZE_FAILED));
        assert(failure.value == original.value && failure.trace == original.trace);
        assert(test.calls == 1 && !process->initializing_file);
        assert(get_object_integer_value(get_own_field_cell(scope, counter_key)->object).value == 1);
        if (!success) {
            const exception_trace_frame_t *again = exception_trace_frame(failure.trace, 0);
            assert(again && again->module == origin_copy.module
                   && again->file_id == origin_copy.file_id
                   && again->instruction == origin_copy.instruction);
        }
        if (object)
            assert(get_object_integer_value(get_object_property(failure.value, value_key)).value
                   == 23);
        clear_exception(&failure);
        destroy_context(next);
        collect_garbage(process);
    }
    assert(!memcmp(module_files_bytecode(files)->buffer, image, image_size));
    assert(!memcmp(module_files_bytecode(other)->buffer, image, image_size));
    free(image);
    destroy_process(process);
    assert(!get_allocated_memory_size());
    puts("OK");
    return 0;
}
