/** @file module_incomplete_scopes_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Incomplete scope rules 36--40 with real VM operations and cyclic file entry.
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/wire/wire.h"
#include "model/module_files.h"
#include "model/thread.h"
#include "vm/vm.h"

#ifdef NDEBUG
#    undef NDEBUG /* Contract checks and fixture calls stay active in Release. */
#endif
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
    field_cell_t *cell;
    unsigned calls[2];
    unsigned rule;
    bool caught;
    bool constant;
} test_t;

static object_t *key(const gchar_t *name, object_static_string_t *storage) {
    return get_static_string_object(storage, name);
}

static int64_t integer(object_t *object, const gchar_t *name) {
    object_static_string_t storage = {0};
    object_t *value = get_object_property(object, key(name, &storage));
    assert(value);
    int_value_t number = get_object_integer_value(value);
    assert(number.has_value);
    return number.value;
}

static void check_keys(object_t *keys, bool ready) {
    object_array_t array = get_array_elements(keys);
    assert(array.size == (ready ? 3 : 2));
    object_static_string_t first = {0}, x = {0}, last = {0};
    assert(!compare_objects_using_vtbl(array.items[0], key(U"first", &first)));
    if (ready)
        assert(!compare_objects_using_vtbl(array.items[1], key(U"x", &x)));
    assert(!compare_objects_using_vtbl(array.items[array.size - 1], key(U"zlast", &last)));
}

static void
observation(process_t *process, object_t *scope, const function_access_t *access, bool ready) {
    object_t *keys = snapshot_object_keys_with_access(process, scope, access);
    check_keys(keys, ready);
    DECREF(keys);
    string_value_t printed = object_to_string_with_access(scope, access);
    char *text = encode_utf8_view((string_view_t){printed.data, printed.length}, NULL);
    assert(text && strstr(text, "first") && strstr(text, "zlast"));
    assert(!strstr(text, "hidden") && !strstr(text, "secret"));
    assert((strstr(text, "\"x\"") != NULL) == ready);
    assert(!strstr(text, "null"));
    FREE(text);
    FREE_STRING(printed);
}

static bool run_frame(module_file_t *file, context_t *frame, exception_t *failure) {
    process_t *process = module_file_process(file);
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
        assert(thread->exception.value);
        *failure = thread->exception;
        thread->exception = (exception_t){0};
    }
    return !status;
}

static void
check_copy(process_t *process, object_t *object, const function_access_t *access, bool allowed) {
    for (unsigned flat = 0; flat < 2; ++flat) {
        operation_result_t result = copy_object_with_access(process, object, access, flat != 0);
        assert(result.value && result.is_exception == !allowed);
        if (allowed)
            assert(result.value != object);
        else
            assert(result.value == get_exception_invalid_operation());
        DECREF(result.value);
    }
}

static bool execute(module_file_t *file, context_t *frame, void *data, exception_t *failure) {
    test_t *test = data;
    unsigned id = module_file_id(file);
    assert(++test->calls[id] == 1 && module_file_state(file) == MODULE_FILE_INITIALIZING);
    process_t *process = module_file_process(file);
    object_t *scope = module_file_scope(test->first);
    static object_static_string_t xs = {0}, target = {0}, caught = {0}, after = {0}, es = {0};
    object_t *x = key(U"x", &xs);
    if (file == test->first) {
        test->cell = get_own_field_cell(scope, x);
        assert(test->cell && test->cell->pending_initialization);
        assert(test->cell->is_constant == test->constant);
        module_file_t *helper = module_files_get(test->files, 1 - id);
        object_t *output = NULL;
        module_initialize_status_t status =
            initialize_module_file(helper, frame, execute, test, &output, failure);
        assert(get_own_field_cell(scope, x) == test->cell);
        assert(test->cell->pending_initialization && test->cell->object == get_null_object());
        assert(module_file_state(file) == MODULE_FILE_INITIALIZING);
        if (!test->caught) {
            assert(status == MODULE_INITIALIZE_FAILED && !output && failure->value);
            return false;
        }
        assert(status == MODULE_INITIALIZE_OK && output == module_file_scope(helper));
        assert(!failure->value);
        return run_frame(file, frame, failure);
    }
    object_t *again = NULL;
    assert(initialize_module_file(test->first, frame, execute, test, &again, failure)
           == MODULE_INITIALIZE_OK);
    assert(again == scope && test->calls[module_file_id(test->first)] == 1);
    assert(create_object_property(frame->data, key(U"target", &target), scope, false) == MSTAT_OK);
    assert(create_object_property(frame->data,
                                  key(U"caught", &caught),
                                  get_static_integer_object(0),
                                  false)
           == MSTAT_OK);
    assert(create_object_property(frame->data,
                                  key(U"after", &after),
                                  get_static_integer_object(0),
                                  false)
           == MSTAT_OK);
    assert(create_object_property(frame->data, key(U"error", &es), get_null_object(), false)
           == MSTAT_OK);
    property_lookup_t pending = lookup_accessible_property(&frame->access, scope, x, false);
    assert(pending.status == MSTAT_UNINITIALIZED && pending.cell == test->cell && !pending.value);
    operation_result_t read = read_accessible_property(&frame->access, scope, x);
    assert(read.is_exception && read.value == get_exception_uninitialized_declaration());
    DECREF(read.value);
    assert(write_accessible_property(&frame->access, scope, x, get_static_integer_object(99))
           == MSTAT_UNINITIALIZED);
    operation_result_t indexed = access_index(process,
                                              &frame->access,
                                              scope,
                                              (object_array_t){&x, 1},
                                              get_static_integer_object(20));
    assert(indexed.is_exception && indexed.value == get_exception_uninitialized_declaration());
    DECREF(indexed.value);
    assert(test->cell->pending_initialization && test->cell->object == get_null_object());
    observation(process, scope, &frame->access, false);
    check_copy(process, scope, &frame->access, false);
    object_t *root = get_root_object();
    object_t *ordinary =
        create_user_defined_object_in_file(process, (object_array_t){&root, 1}, frame->module, id);
    check_copy(process, ordinary, &frame->access, true);
    DECREF(ordinary);
    return run_frame(file, frame, failure);
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
    module_file_t *helper = module_files_get(files, 1 - module_file_id(first));
    object_t *scope = module_file_scope(first);
    object_static_string_t fs = {0}, hs = {0}, zs = {0}, xs = {0}, ss = {0}, seen = {0},
                           child = {0};
    object_t *first_key = key(U"first", &fs), *hidden = key(U"hidden", &hs);
    object_t *last = key(U"zlast", &zs), *x = key(U"x", &xs), *secret = key(U"secret", &ss);
    module_t *module = module_files_identity(files);
    access_domain_t *domain = get_object_access_domain(scope);
    assert(create_object_field_in_file(scope,
                                       last,
                                       get_static_integer_object(99),
                                       false,
                                       ACCESS_PUBLIC,
                                       module,
                                       module_file_id(first),
                                       domain,
                                       true));
    assert(create_object_field_in_file(scope,
                                       first_key,
                                       get_static_integer_object(1),
                                       false,
                                       ACCESS_PUBLIC,
                                       module,
                                       module_file_id(first),
                                       domain,
                                       true));
    assert(create_object_field_in_file(scope,
                                       hidden,
                                       get_static_integer_object(77),
                                       false,
                                       ACCESS_PRIVATE,
                                       module,
                                       module_file_id(first),
                                       domain,
                                       true));
    context_t *observer = create_module_file_context(helper, process->main_thread->context);
    check_copy(process, scope, &observer->access, false);
    test_t test = {.files = files,
                   .first = first,
                   .rule = (unsigned)atoi(argv[1]),
                   .caught = atoi(argv[2]) != 0,
                   .constant = atoi(argv[3]) != 0};
    exception_t failure = {0};
    object_t *output = NULL;
    module_initialize_status_t status = initialize_module_file(first,
                                                               process->main_thread->context,
                                                               execute,
                                                               &test,
                                                               &output,
                                                               &failure);
    assert(test.calls[0] == 1 && test.calls[1] == 1);
    object_t *helper_scope = module_file_scope(helper);
    check_keys(get_object_property(helper_scope, key(U"seen", &seen)), false);
    assert(integer(helper_scope, U"caught") == (test.caught ? 1 : 0));
    assert(integer(helper_scope, U"after") == (test.caught ? 1 : 0));
    object_static_string_t error_storage = {0};
    object_t *error = test.caught ? get_object_property(helper_scope, key(U"error", &error_storage))
                                  : failure.value;
    assert(error);
    if (test.rule == 39) {
        assert(error == get_exception_invalid_operation());
    } else {
        string_value_t rendered = object_to_string_with_access(error, NULL);
        char *message = encode_utf8_view((string_view_t){rendered.data, rendered.length}, NULL);
        assert(message && strstr(message, "UNINITIALIZED_DECLARATION"));
        FREE(message);
        FREE_STRING(rendered);
    }
    object_t *descendant = NULL;
    if (test.rule == 40) {
        descendant = get_object_property(helper_scope, key(U"child", &child));
        assert(descendant);
        assert(get_inheritance_prototypes(descendant).items[0] == scope);
        assert(integer(descendant, U"own") == 5);
    }
    if (test.caught) {
        assert(status == MODULE_INITIALIZE_OK && output == scope && !failure.value);
        assert(module_file_state(first) == MODULE_FILE_INITIALIZED);
        assert(module_file_state(helper) == MODULE_FILE_INITIALIZED);
        assert(get_own_field_cell(scope, x) == test.cell && !test.cell->pending_initialization);
        assert(integer(scope, U"x") == 10);
        assert(lookup_accessible_property(&observer->access, scope, secret, false).status
               == MSTAT_ACCESS_DENIED);
        observation(process, scope, &observer->access, true);
        check_copy(process, scope, &observer->access, true);
        if (descendant) {
            assert(get_inheritance_prototypes(descendant).items[0] == scope);
            assert(integer(descendant, U"own") == 5);
            assert(lookup_accessible_property(&observer->access, descendant, x, false).value
                   == test.cell->object);
        }
        assert(write_accessible_property(&observer->access, scope, x, get_static_integer_object(21))
               == (test.constant ? MSTAT_PROPERTY_IS_CONSTANT : MSTAT_OK));
        assert(integer(scope, U"x") == (test.constant ? 10 : 21));
        if (descendant)
            assert(get_object_integer_value(
                       lookup_accessible_property(&observer->access, descendant, x, false).value)
                       .value
                   == (test.constant ? 10 : 21));
    } else {
        assert(status == MODULE_INITIALIZE_FAILED && !output && failure.value);
        assert(module_file_state(first) == MODULE_FILE_FAILED
               && module_file_state(helper) == MODULE_FILE_FAILED);
        assert(module_file_failure(first) == failure.value
               && module_file_failure(helper) == failure.value);
        assert(test.cell->pending_initialization && test.cell->object == get_null_object());
        clear_exception(&failure);
    }
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        assert(initialize_module_file(first,
                                      process->main_thread->context,
                                      execute,
                                      &test,
                                      &output,
                                      &failure)
               == status);
        clear_exception(&failure);
    }
    assert(test.calls[0] == 1 && test.calls[1] == 1);
    assert(!memcmp(saved, code->buffer, code->buffer_size));
    free(saved);
    destroy_context(observer);
    destroy_process(process);
    assert(!get_allocated_memory_size());
    puts("OK");
    return 0;
}
