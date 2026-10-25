/** @file module_initialization_errors_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Saved exception identity, scope revocation and escaped function guards.
 */
#include "lib/allocate.h"
#include "lib/file_system.h"
#include "lib/wire.h"
#include "model/module_files.h"
#include "model/thread.h"
#include "vm/gc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

typedef struct {
    object_t *payload;
    unsigned calls;
    bool success;
    bool contradictory;
} test_t;

static bool execute(module_file_t *file, context_t *frame, void *data, exception_t *failure) {
    test_t *test = data;
    assert(frame->data == module_file_scope(file));
    ++test->calls;
    if (test->success)
        return true;
    failure->value = test->payload;
    INCREF(failure->value);
    return test->contradictory;
}

static void clear_exception(thread_t *thread, object_t *expected) {
    assert(thread->exception.value == expected);
    DECREF(thread->exception.value);
    thread->exception.value = NULL;
}

static void blocked_call(object_t *fn, object_t *receiver, thread_t *thread, object_t *payload) {
    context_t *context = thread->context;
    size_t stack = thread->data_stack->size, attempts = thread->native_attempts;
    instr_index_t instruction = thread->instr_id;
    assert(!call_object(fn, 0, receiver, thread));
    assert(thread->context == context && thread->data_stack->size == stack);
    assert(thread->instr_id == instruction && thread->native_attempts == attempts);
    clear_exception(thread, payload);
}

int main(int argc, char **argv) {
    assert(argc == 2);
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    size_t capacity = 1024 * 1024;
    unsigned char *input = malloc(capacity);
    assert(input);
    size_t size = FREAD(input, 1, capacity, stdin);
    assert(size > 4 && size < capacity && !FERROR(stdin));
    uint32_t json_size = read_le32(input);
    assert(json_size < size - 4);
    process_t *process = create_process();
    module_files_t *files = NULL;
    assert(create_module_files(process,
                               (char *)input + 4,
                               json_size,
                               input + 4 + json_size,
                               size - 4 - json_size,
                               &files,
                               NULL)
           == MODULE_BYTECODE_OK);
    free(input);
    module_file_t *file = module_files_main(files);
    module_file_t *good = module_files_get(files, (module_file_id(file) + 1) % 3);
    thread_t *thread = process->main_thread;
    object_t *scope = module_file_scope(file), *root = get_root_object();
    context_t *frame = create_module_file_context(file, thread->context);
    object_static_string_t visible_storage = {0}, value_storage = {0}, method_storage = {0};
    object_t *visible = get_static_string_object(&visible_storage, U"visible");
    object_t *value_key = get_static_string_object(&value_storage, U"value");
    object_t *method_key = get_static_string_object(&method_storage, U"method");
    assert(create_object_property(scope, visible, get_static_integer_object(41), false)
           == MSTAT_OK);
    object_t *ordinary = create_user_defined_object_in_file(process,
                                                            (object_array_t){&root, 1},
                                                            frame->module,
                                                            module_file_id(file));
    assert(create_object_property(ordinary, value_key, get_static_integer_object(7), false)
           == MSTAT_OK);
    object_t *function = create_function_object(process, NULL, 0, 0, scope, NULL, false);
    assert(create_object_property(ordinary, method_key, function, false) == MSTAT_OK);
    object_t *copy = clone_object_with_access(process, ordinary, &frame->access);
    object_t *flat = flat_object_with_access(process, ordinary, &frame->access);
    object_t *function_copy = clone_object_with_access(process, function, &frame->access);
    assert(copy && flat && function_copy);
    object_t *payload;
    if (!strcmp(argv[1], "null"))
        payload = get_null_object();
    else if (!strcmp(argv[1], "integer"))
        payload = create_integer_object(process, 9001);
    else {
        payload = create_user_defined_object(process, (object_array_t){&root, 1});
        assert(create_object_property(payload, value_key, get_static_integer_object(1), false)
               == MSTAT_OK);
    }
    test_t test = {payload, 0, false, false};
    exception_t error = {0};
    object_t *output = NULL;
    assert(initialize_module_file(file, thread->context, execute, &test, &output, &error)
           == MODULE_INITIALIZE_FAILED);
    assert(!output && error.value == payload && test.calls == 1);
    assert(module_file_failure(file) == payload && module_scope_failure(scope) == payload);
    assert(module_files_failure(process, frame->module, module_file_id(file)) == payload);
    DECREF(error.value);
    error.value = NULL;
    /* Only the saved failure roots the payload; surviving ordinary values are rooted separately. */
    object_t *roots[] = {ordinary, copy, flat, function, function_copy};
    for (size_t i = 0; i < sizeof(roots) / sizeof(*roots); ++i)
        add_gc_root(process, roots[i]);
    DECREF(payload);
    collect_garbage(process);
    payload = module_file_failure(file);
    assert(payload && !module_scope_failure(ordinary));
    if (!strcmp(argv[1], "object")) {
        assert(set_object_property(payload, value_key, get_static_integer_object(99)) == MSTAT_OK);
        assert(get_object_integer_value(get_object_property(payload, value_key)).value == 99);
    }
    for (unsigned i = 0; i < 4; ++i) {
        assert(initialize_module_file(file, thread->context, execute, &test, &output, &error)
               == MODULE_INITIALIZE_FAILED);
        assert(!output && error.value == payload && test.calls == 1);
        DECREF(error.value);
        error.value = NULL;
    }
    property_lookup_t lookup = lookup_accessible_property(&frame->access, scope, visible, false);
    assert(lookup.status == MSTAT_FAILED_FILE && lookup.value == payload);
    lookup = lookup_accessible_property(&frame->access, scope, value_key, true);
    assert(lookup.status == MSTAT_FAILED_FILE && lookup.value == payload);
    operation_result_t write = write_accessible_property_result(&frame->access,
                                                                scope,
                                                                visible,
                                                                get_static_integer_object(42));
    assert(write.is_exception && write.value == payload);
    DECREF(write.value);
    assert(get_object_integer_value(get_own_field_cell(scope, visible)->object).value == 41);
    assert(object_observation_failure(scope, &frame->access) == payload);
    assert(!object_observation_failure(ordinary, &frame->access));
    object_t *array = create_array_object(process, (object_array_t){&scope, 1});
    assert(object_observation_failure(array, &frame->access) == payload);
    DECREF(array);
    object_t *descendant = create_user_defined_object_in_file(process,
                                                              (object_array_t){&scope, 1},
                                                              frame->module,
                                                              module_file_id(file));
    operation_result_t copied = copy_object_with_access(process, descendant, &frame->access, false);
    assert(!copied.is_exception && copied.value);
    assert(get_object_prototypes(copied.value).items[0] == scope);
    DECREF(copied.value);
    copied = copy_object_with_access(process, descendant, &frame->access, true);
    assert(copied.is_exception && copied.value == payload);
    DECREF(copied.value);
    DECREF(descendant);
    blocked_call(get_function_clone(), scope, thread, payload);
    blocked_call(get_function_flat(), scope, thread, payload);
    blocked_call(function, NULL, thread, payload);
    blocked_call(function_copy, NULL, thread, payload);
    blocked_call(get_object_property(copy, method_key), copy, thread, payload);
    blocked_call(get_object_property(flat, method_key), flat, thread, payload);
    assert(
        write_accessible_property(&frame->access, ordinary, value_key, get_static_integer_object(8))
        == MSTAT_OK);
    assert(get_object_integer_value(get_object_property(ordinary, value_key)).value == 8);
    assert(call_object(get_function_clone(), 0, ordinary, thread));
    object_t *ordinary_copy = pop_object_from_stack(thread->data_stack);
    DECREF(ordinary_copy);
    test_t successful = {NULL, 0, true, false};
    assert(initialize_module_file(good, thread->context, execute, &successful, &output, &error)
           == MODULE_INITIALIZE_OK);
    assert(output == module_file_scope(good) && !error.value && successful.calls == 1);
    assert(!module_file_failure(good));
    module_file_t *contradictory = module_files_get(files, (module_file_id(file) + 2) % 3);
    test_t inconsistent = {payload, 0, false, true};
    assert(initialize_module_file(contradictory,
                                  thread->context,
                                  execute,
                                  &inconsistent,
                                  &output,
                                  &error)
           == MODULE_INITIALIZE_FAILED);
    assert(error.value == payload && module_file_failure(contradictory) == payload);
    DECREF(error.value);
    error.value = NULL;
    assert(initialize_module_file(contradictory,
                                  thread->context,
                                  execute,
                                  &inconsistent,
                                  &output,
                                  &error)
           == MODULE_INITIALIZE_FAILED);
    assert(error.value == payload && inconsistent.calls == 1);
    DECREF(error.value);
    error.value = NULL;
    destroy_context(frame);
    for (size_t i = 0; i < sizeof(roots) / sizeof(*roots); ++i) {
        remove_gc_root(process, roots[i]);
        DECREF(roots[i]);
    }
    destroy_process(process);
    assert(!get_allocated_memory_size());
    puts("OK");
    return 0;
}
