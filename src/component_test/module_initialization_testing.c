/** @file module_initialization_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Lazy file state and real VM execution over independent wire fixtures.
 */
#include "lib/allocate.h"
#include "lib/file_system.h"
#include "lib/wire.h"
#include "model/module_files.h"
#include "model/thread.h"
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
    size_t calls[3];
    bool fail;
    const char *mode;
} test_t;

static object_t *key(const gchar_t *text, object_static_string_t *storage) {
    return get_static_string_object(storage, text);
}

static bool execute(module_file_t *file, context_t *frame, void *data) {
    test_t *test = data;
    process_t *process = module_file_process(file);
    uint32_t id = module_file_id(file);
    assert(id < 3 && ++test->calls[id] == 1);
    assert(module_file_state(file) == MODULE_FILE_INITIALIZING);
    assert(frame->data == module_file_scope(file));
    assert(frame->module == module_files_identity(test->files));
    bool returns = !strcmp(test->mode, "return");
    object_static_string_t xs = {0}, cs = {0}, zs = {0}, is = {0}, fs = {0};
    object_t *x = key(U"x", &xs), *c = key(U"c", &cs), *z = key(U"nested", &zs);
    object_t *inner = key(U"inner", &is), *cleanup = key(U"cleanup", &fs);
    assert(!get_own_field_cell(frame->data, inner) && !get_own_field_cell(frame->data, cleanup));
    property_lookup_t pending = lookup_accessible_property(&frame->access, frame->data, x, false);
    if (!returns) {
        assert(pending.status == MSTAT_UNINITIALIZED && pending.cell && !pending.value);

        assert(lookup_accessible_property(&frame->access, frame->data, c, false).status
               == MSTAT_UNINITIALIZED);
        assert(!get_own_field_cell(frame->data, z));
        assert(
            write_accessible_property(&frame->access, frame->data, x, get_static_integer_object(1))
            == MSTAT_UNINITIALIZED);
        operation_result_t error = access_status_result(pending.status, NULL);
        assert(error.is_exception && error.value);
        DECREF(error.value);
    }
    field_cell_t *cell = pending.cell;
    if (test->fail)
        return false;
    object_t *scope = NULL;
    bool fails = !strcmp(test->mode, "early") || returns;
    if (!fails && file == test->first) {
        module_file_t *second = module_files_get(test->files, 1 - id);
        assert(module_file_state(second) == MODULE_FILE_UNINITIALIZED);
        assert(initialize_module_file(second, frame, execute, test, &scope)
               == MODULE_INITIALIZE_OK);
        assert(scope == module_file_scope(second));
        assert(module_file_state(second) == MODULE_FILE_INITIALIZED);
    } else if (!fails) {
        assert(initialize_module_file(test->first, frame, execute, test, &scope)
               == MODULE_INITIALIZE_OK);
        assert(scope == module_file_scope(test->first));
        assert(lookup_accessible_property(&frame->access, scope, x, false).status
               == MSTAT_UNINITIALIZED);
        string_value_t chain = module_initialization_chain(process, test->first);
        char *utf8 = encode_utf8_view((string_view_t){chain.data, chain.length}, NULL);
        assert(utf8 && strstr(utf8, " -> ") && strstr(strstr(utf8, " -> ") + 4, " -> "));
        assert(strstr(utf8, "file 0") && strstr(utf8, "file 1"));
        FREE(utf8);
        FREE_STRING(chain);
    }
    thread_t *thread = process->main_thread;
    context_t *previous = thread->context;
    instr_index_t address = thread->instr_id;
    if (fails) {
        object_t *sentinel = get_static_integer_object(31);
        INCREF(sentinel);
        push_object_onto_stack(thread->data_stack, sentinel);
    }
    thread->context = frame;
    thread->instr_id = module_file_entry(file);
    int status = run(process, module_file_bytecode(file));
    assert(status == (fails ? 1 : 0));
    assert(thread->context == frame && thread->data_stack->size == (fails ? 1 : 0));
    if (fails) {
        object_t *sentinel = pop_object_from_stack(thread->data_stack);
        assert(get_object_integer_value(sentinel).value == 31);
        DECREF(sentinel);
    }
    thread->context = previous;
    thread->instr_id = address;
    if (fails) {
        assert(thread->exception.value);
        if (!returns) {
            string_value_t text = object_to_string_with_access(thread->exception.value, NULL);
            char *utf8 = encode_utf8_view((string_view_t){text.data, text.length}, NULL);
            assert(utf8 && strstr(utf8, "UNINITIALIZED_DECLARATION") && strstr(utf8, "file "));
            FREE(utf8);
            FREE_STRING(text);
        }
        return false;
    }
    if (!strcmp(test->mode, "caught")) {
        object_static_string_t es = {0};
        object_t *exception = get_object_property(frame->data, key(U"error", &es));
        assert(exception);
        string_value_t text = object_to_string_with_access(exception, NULL);
        char *utf8 = encode_utf8_view((string_view_t){text.data, text.length}, NULL);
        assert(utf8 && strstr(utf8, "UNINITIALIZED_DECLARATION") && strstr(utf8, "file "));
        FREE(utf8);
        FREE_STRING(text);
    }
    property_lookup_t ready = lookup_accessible_property(&frame->access, frame->data, x, false);
    assert(ready.status == MSTAT_OK && ready.cell == cell);
    int_value_t value = get_object_integer_value(ready.value);
    assert(value.has_value && value.value == 100 + id);
    assert(!get_own_field_cell(frame->data, z));
    assert(write_accessible_property(&frame->access, frame->data, c, get_static_integer_object(1))
           == MSTAT_PROPERTY_IS_CONSTANT);
    assert(!get_own_field_cell(frame->data, inner) && !get_own_field_cell(frame->data, cleanup));
    return true;
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
    module_files_t *files = NULL, *other = NULL;
    assert(create_module_files(process,
                               (char *)input + 4,
                               json_size,
                               input + 4 + json_size,
                               size - 4 - json_size,
                               &files,
                               NULL)
           == MODULE_BYTECODE_OK);
    assert(create_module_files(process,
                               (char *)input + 4,
                               json_size,
                               input + 4 + json_size,
                               size - 4 - json_size,
                               &other,
                               NULL)
           == MODULE_BYTECODE_OK);
    free(input);
    bytecode_t *code = module_files_bytecode(files);
    unsigned char *copy = malloc(code->buffer_size);
    assert(copy);
    memcpy(copy, code->buffer, code->buffer_size);
    module_file_t *first = module_files_main(files);
    assert(module_file_id(first) < 2 && module_files_count(files) == 3);
    test_t test = {.files = files, .first = first, .mode = argv[1]};
    context_t *caller = process->main_thread->context;
    object_t *scope = (void *)(uintptr_t)1;
    assert(initialize_module_file(NULL, caller, execute, &test, &scope)
               == MODULE_INITIALIZE_ARGUMENT
           && !scope);
    assert(initialize_module_file(first, NULL, execute, &test, &scope)
           == MODULE_INITIALIZE_ARGUMENT);
    assert(initialize_module_file(first, caller, NULL, &test, &scope)
           == MODULE_INITIALIZE_ARGUMENT);
    assert(initialize_module_file(first, caller, execute, &test, NULL)
           == MODULE_INITIALIZE_ARGUMENT);
    process_t *foreign = create_process();
    assert(initialize_module_file(first, foreign->main_thread->context, execute, &test, &scope)
           == MODULE_INITIALIZE_ARGUMENT);
    destroy_process(foreign);
    assert(module_file_state(first) == MODULE_FILE_UNINITIALIZED);
    if (!strcmp(argv[1], "early") || !strcmp(argv[1], "return")) {
        assert(initialize_module_file(first, caller, execute, &test, &scope)
               == MODULE_INITIALIZE_FAILED);
        assert(!scope && module_file_state(first) == MODULE_FILE_FAILED);
        assert(process->main_thread->context == caller && process->main_thread->exception.value);
        assert(initialize_module_file(first, caller, execute, &test, &scope)
               == MODULE_INITIALIZE_FAILED);
        assert(test.calls[module_file_id(first)] == 1);
        assert(!memcmp(copy, code->buffer, code->buffer_size));
        free(copy);
        destroy_process(process);
        assert(!get_allocated_memory_size());
        puts("OK");
        return 0;
    }
    assert(initialize_module_file(first, caller, execute, &test, &scope) == MODULE_INITIALIZE_OK);
    assert(scope == module_file_scope(first));
    assert(module_file_state(first) == MODULE_FILE_INITIALIZED);
    assert(module_file_state(module_files_get(files, 2)) == MODULE_FILE_UNINITIALIZED);
    assert(test.calls[0] == 1 && test.calls[1] == 1 && !test.calls[2]);
    context_t *frame = create_module_file_context(first, caller);
    object_static_string_t xs = {0};
    object_t *x = key(U"x", &xs);
    assert(write_accessible_property(&frame->access, scope, x, get_static_integer_object(37))
           == MSTAT_OK);
    destroy_context(frame);
    for (unsigned i = 0; i < 4; ++i)
        assert(initialize_module_file(first, caller, execute, &test, &scope)
               == MODULE_INITIALIZE_OK);
    assert(get_object_integer_value(get_own_field_cell(scope, x)->object).value == 37);
    assert(test.calls[0] == 1 && test.calls[1] == 1);
    string_value_t empty = module_initialization_chain(process, NULL);
    assert(empty.length == 0);
    FREE_STRING(empty);
    assert(!memcmp(copy, code->buffer, code->buffer_size));
    free(copy);
    module_file_t *failed = module_files_main(other);
    assert(module_file_state(failed) == MODULE_FILE_UNINITIALIZED);
    test_t failure = {.files = other, .first = failed, .fail = true, .mode = argv[1]};
    assert(initialize_module_file(failed, caller, execute, &failure, &scope)
               == MODULE_INITIALIZE_FAILED
           && !scope);
    assert(module_file_state(failed) == MODULE_FILE_FAILED);
    assert(initialize_module_file(failed, caller, execute, &failure, &scope)
               == MODULE_INITIALIZE_FAILED
           && !scope);
    assert(failure.calls[module_file_id(failed)] == 1);
    empty = module_initialization_chain(process, NULL);
    assert(!empty.length);
    FREE_STRING(empty);
    destroy_process(process);
    assert(!get_allocated_memory_size());
    puts("OK");
    return 0;
}
