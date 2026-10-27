/** @file module_failed_prototypes_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Rules 41--45 with cyclic file initialization and real VM copying.
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
    module_files_t *files;
    module_file_t *first;
    object_t *payload;
    object_t *child;
    unsigned calls[3];
    unsigned shape;
    bool fail;
} test_t;

static object_t *key(const gchar_t *name) {
    /* Keys outlive every field and all VM runs in this process. */
    static object_static_string_t storage[16] = {0};
    static const gchar_t *names[16] = {0};
    for (unsigned i = 0; i < 16; ++i) {
        if (!names[i]) {
            names[i] = name;
            return get_static_string_object(storage + i, name);
        }
        if (!STRCMP(names[i], name))
            return get_static_string_object(storage + i, name);
    }
    abort();
}

static void field(object_t *object, const gchar_t *name, object_t *value) {
    assert(create_object_field_in_file(object,
                                       key(name),
                                       value,
                                       false,
                                       ACCESS_PUBLIC,
                                       get_object_module(object),
                                       get_object_file_id(object),
                                       get_object_access_domain(object),
                                       true));
}

static int64_t number(object_t *object, const gchar_t *name) {
    object_t *value = get_object_property(object, key(name));
    assert(value);
    int_value_t result = get_object_integer_value(value);
    assert(result.has_value);
    return result.value;
}

static object_t *node(process_t *process, context_t *frame, object_t **parents, size_t count) {
    return create_user_defined_object_in_file(process,
                                              (object_array_t){parents, count},
                                              frame->module,
                                              frame->access.file_id);
}

static void read_value(object_t *object,
                       const function_access_t *access,
                       const gchar_t *name,
                       object_t *expected,
                       bool failed) {
    operation_result_t result = read_accessible_property(access, object, key(name));
    assert(result.is_exception == failed && result.value == expected);
    DECREF(result.value);
}

static void check_graph(test_t *test, context_t *frame, bool ready) {
    process_t *process = module_file_process(test->first);
    object_t *scope = module_file_scope(test->first);
    object_t *child = test->child;
    const function_access_t *access = &frame->access;
    object_t *failure = module_file_failure(test->first);
    assert(number(child, U"own") == 20);
    read_value(child, access, U"own", get_static_integer_object(20), false);
    read_value(child,
               access,
               U"value",
               failure ? failure : get_static_integer_object(10),
               failure != NULL);
    field_cell_t *own = get_own_field_cell(child, key(U"own"));
    object_array_t before = get_inheritance_prototypes(child);
    object_t *parents[4];
    assert(before.size <= 4);
    memcpy(parents, before.items, before.size * sizeof(*parents));
    operation_result_t clone = copy_object_with_access(process, child, access, false);
    assert(!clone.is_exception && clone.value && clone.value != child);
    assert(number(clone.value, U"own") == 20);
    object_array_t cloned_parents = get_inheritance_prototypes(clone.value);
    assert(cloned_parents.size == before.size);
    for (size_t i = 0; i < before.size; ++i)
        assert(cloned_parents.items[i] == parents[i]);
    read_value(clone.value,
               access,
               U"value",
               failure ? failure : get_static_integer_object(10),
               failure != NULL);
    assert(set_object_property(clone.value, key(U"own"), get_static_integer_object(21))
           == MSTAT_OK);
    assert(number(child, U"own") == 20);
    DECREF(clone.value);
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        operation_result_t flat = copy_object_with_access(process, child, access, true);
        assert(flat.is_exception == !ready && flat.value);
        if (ready) {
            assert(flat.value != child && number(flat.value, U"own") == 20);
            assert(number(flat.value, U"value") == 10);
            assert(get_own_field_cell(flat.value, key(U"value")));
            if (test->shape == 3)
                assert(number(flat.value, U"winner") == 11);
            object_array_t keys = get_object_keys(flat.value);
            size_t found = 0;
            for (size_t i = 0; i < keys.size; ++i)
                found += !compare_objects_using_vtbl(keys.items[i], key(U"value"));
            assert(found == 1);
            assert(set_object_property(flat.value, key(U"own"), get_static_integer_object(22))
                   == MSTAT_OK);
        } else {
            assert(flat.value == (failure ? failure : get_exception_invalid_operation()));
            if (failure)
                assert(flat.trace == module_file_exception(test->first).trace);
        }
        DECREF(flat.value);
        assert(get_own_field_cell(child, key(U"own")) == own && number(child, U"own") == 20);
        assert(number(scope, U"value") == 10);
        object_array_t after = get_inheritance_prototypes(child);
        assert(after.size == before.size);
        for (size_t i = 0; i < before.size; ++i)
            assert(after.items[i] == parents[i]);
    }
    for (unsigned flat = 0; flat < 2; ++flat) {
        operation_result_t result = copy_object_with_access(process, scope, access, flat != 0);
        assert(result.is_exception == !ready);
        if (!ready)
            assert(result.value == (failure ? failure : get_exception_invalid_operation()));
        DECREF(result.value);
    }
}

static void check_clone_guards(test_t *test, context_t *frame) {
    process_t *process = module_file_process(test->first);
    object_t *scope = module_file_scope(test->first);
    object_t *payload = module_file_failure(test->first);
    assert(payload);
    read_value(scope, &frame->access, U"clone", payload, true);
    operation_result_t written = write_accessible_property_result(&frame->access,
                                                                  test->child,
                                                                  key(U"clone"),
                                                                  get_static_integer_object(17));
    assert(written.is_exception && written.value == payload);
    DECREF(written.value);
    assert(!get_own_field_cell(test->child, key(U"clone")));
    object_t *own = node(process, frame, &scope, 1);
    field(own, U"clone", get_static_integer_object(17));
    read_value(own, &frame->access, U"clone", get_static_integer_object(17), false);
    field_cell_t *cell = get_own_field_cell(own, key(U"clone"));
    cell->modifier = ACCESS_PRIVATE;
    operation_result_t private = read_accessible_property(&frame->access, own, key(U"clone"));
    assert(private.is_exception && private.value != get_function_clone());
    DECREF(private.value);
    cell->modifier = ACCESS_PUBLIC;
    cell->pending_initialization = true;
    operation_result_t pending = read_accessible_property(&frame->access, own, key(U"clone"));
    assert(pending.is_exception && pending.value == get_exception_uninitialized_declaration());
    DECREF(pending.value);
    cell->pending_initialization = false;
    object_t *leaf = node(process, frame, &own, 1);
    read_value(leaf, &frame->access, U"clone", get_static_integer_object(17), false);
    DECREF(leaf);
    DECREF(own);
    /* A failed ancestor's explicit declaration must never become a builtin fallback,
     * even if hidden or not yet initialized. Inspecting it is not reading its value. */
    field(scope, U"clone", get_static_integer_object(19));
    cell = get_own_field_cell(scope, key(U"clone"));
    for (unsigned variant = 0; variant < 3; ++variant) {
        cell->modifier = variant == 1 ? ACCESS_PRIVATE : ACCESS_PUBLIC;
        cell->pending_initialization = variant == 2;
        read_value(test->child, &frame->access, U"clone", payload, true);
    }
}

static void run_observer(test_t *test, context_t *caller, bool ready) {
    module_file_t *file = module_files_get(test->files, (module_file_id(test->first) + 2) % 3);
    process_t *process = module_file_process(file);
    thread_t *thread = process->main_thread;
    context_t *frame = create_module_file_context(file, caller);
    object_t *scope = frame->data;
    const gchar_t *names[] = {U"child", U"flatfn", U"result", U"error", U"seen", U"copied"};
    object_t *values[] = {test->child,
                          get_function_flat(),
                          get_static_integer_object(88),
                          get_null_object(),
                          get_static_integer_object(0),
                          get_null_object()};
    for (unsigned i = 0; i < 6; ++i) {
        model_status_t status =
            get_own_field_cell(scope, key(names[i]))
                ? set_object_property(scope, key(names[i]), values[i])
                : create_object_property(scope, key(names[i]), values[i], false);
        assert(status == MSTAT_OK);
    }
    context_t *previous = thread->context;
    instr_index_t address = thread->instr_id;
    size_t before = thread->data_stack->size;
    thread->context = frame;
    thread->instr_id = module_file_entry(file);
    assert(run(process, module_file_bytecode(file)) == 0);
    assert(thread->context == frame && thread->data_stack->size == before);
    assert(number(scope, U"seen") == 20);
    object_t *copied = get_object_property(scope, key(U"copied"));
    assert(copied != test->child && number(copied, U"own") == 20);
    object_array_t original_parents = get_inheritance_prototypes(test->child);
    object_array_t copied_parents = get_inheritance_prototypes(copied);
    assert(original_parents.size == copied_parents.size);
    for (size_t i = 0; i < original_parents.size; ++i)
        assert(original_parents.items[i] == copied_parents.items[i]);
    if (ready) {
        object_t *result = get_object_property(scope, key(U"result"));
        assert(result != test->child && number(result, U"own") == 20);
        assert(number(result, U"value") == 10);
        assert(get_object_property(scope, key(U"error")) == get_null_object());
    } else {
        assert(number(scope, U"result") == 88); /* No partial flattened value escaped. */
        object_t *expected = module_file_failure(test->first);
        if (!expected)
            expected = get_exception_invalid_operation();
        assert(get_object_property(scope, key(U"error")) == expected);
    }
    thread->context = previous;
    thread->instr_id = address;
    destroy_context(frame);
}

static bool execute(module_file_t *file, context_t *frame, void *data, exception_t *failure) {
    test_t *test = data;
    unsigned id = module_file_id(file);
    assert(++test->calls[id] == 1);
    assert(module_file_state(file) == MODULE_FILE_INITIALIZING);
    object_t *scope = module_file_scope(test->first);
    process_t *process = module_file_process(file);
    if (file == test->first) {
        field(scope, U"value", get_static_integer_object(10));
        field(scope, U"winner", get_static_integer_object(22));
        module_file_t *helper = module_files_get(test->files, (id + 1) % 3);
        object_t *output = NULL;
        assert(initialize_module_file(helper, frame, execute, test, &output, failure)
               == MODULE_INITIALIZE_OK);
        assert(output == module_file_scope(helper) && !failure->value);
        assert(module_file_state(helper) == MODULE_FILE_INITIALIZED);
        if (test->fail) {
            failure->value = test->payload;
            INCREF(failure->value);
            failure->trace = capture_exception_trace(frame, module_file_entry(file));
            return false;
        }
        return true;
    }
    object_t *again = NULL;
    assert(initialize_module_file(test->first, frame, execute, test, &again, failure)
           == MODULE_INITIALIZE_OK);
    assert(again == scope && test->calls[module_file_id(test->first)] == 1);
    object_t *middle = NULL, *other = NULL, *root = get_root_object();
    object_t *parents[2] = {scope, NULL};
    size_t count = 1;
    if (test->shape) {
        middle = node(process, frame, parents, 1);
        field(middle, U"middle", get_static_integer_object(30));
        parents[0] = middle;
    }
    if (test->shape == 2) {
        other = node(process, frame, &scope, 1);
        parents[1] = other;
        count = 2; /* A shared ancestor must be checked and transferred only once. */
    } else if (test->shape == 3) {
        other = node(process, frame, &root, 1);
        field(other, U"winner", get_static_integer_object(11));
        parents[0] = other;
        parents[1] = middle; /* A healthy first branch cannot hide the incomplete later branch. */
        count = 2;
    }
    test->child = node(process, frame, parents, count);
    field(test->child, U"own", get_static_integer_object(20));
    field(frame->data, U"child", test->child);
    if (middle)
        DECREF(middle);
    if (other)
        DECREF(other);
    check_graph(test, frame, false);
    run_observer(test, frame, false);
    return true;
}

int main(int argc, char **argv) {
    assert(argc == 4);
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
    bytecode_t *code = module_files_bytecode(files);
    unsigned char *saved = malloc(code->buffer_size);
    assert(saved);
    memcpy(saved, code->buffer, code->buffer_size);
    module_file_t *first = module_files_main(files);
    object_t *root = get_root_object();
    object_t *payload = !strcmp(argv[3], "null")
                            ? get_null_object()
                            : create_user_defined_object(process, (object_array_t){&root, 1});
    add_gc_root(process, payload);
    test_t test = {.files = files,
                   .first = first,
                   .payload = payload,
                   .shape = (unsigned)atoi(argv[1]),
                   .fail = atoi(argv[2]) != 0};
    object_t *output = NULL;
    exception_t failure = {0};
    module_initialize_status_t status = initialize_module_file(first,
                                                               process->main_thread->context,
                                                               execute,
                                                               &test,
                                                               &output,
                                                               &failure);
    assert(status == (test.fail ? MODULE_INITIALIZE_FAILED : MODULE_INITIALIZE_OK));
    assert(module_file_state(first) == (test.fail ? MODULE_FILE_FAILED : MODULE_FILE_INITIALIZED));
    if (test.fail) {
        assert(!output && failure.value == payload && failure.trace);
        assert(exception_trace_frame(failure.trace, 0)->file_id == module_file_id(first));
    } else {
        assert(output == module_file_scope(first) && !failure.value);
    }
    context_t *observer =
        create_module_file_context(module_files_get(files, (module_file_id(first) + 2) % 3),
                                   process->main_thread->context);
    check_graph(&test, observer, !test.fail);
    run_observer(&test, observer, !test.fail);
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        clear_exception(&failure);
        assert(initialize_module_file(first, observer, execute, &test, &output, &failure)
               == status);
        if (test.fail)
            assert(failure.value == payload && failure.trace == module_file_exception(first).trace);
    }
    if (test.fail)
        check_clone_guards(&test, observer);
    assert(test.calls[module_file_id(first)] == 1);
    assert(test.calls[(module_file_id(first) + 1) % 3] == 1);
    assert(test.calls[(module_file_id(first) + 2) % 3] == 0);
    assert(!memcmp(saved, code->buffer, code->buffer_size));
    free(saved);
    clear_exception(&failure);
    destroy_context(observer);
    DECREF(test.child);
    remove_gc_root(process, payload);
    DECREF(payload);
    destroy_process(process);
    assert(!get_allocated_memory_size());
    puts("OK");
    return 0;
}
