/** @file module_files_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Exercises isolated process-owned file scopes over independent wire fixtures.
 */
/* Fixture setup and validation must also run against the optimized Release core. */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/wire/wire.h"
#include "model/context.h"
#include "model/module_files.h"
#include "model/thread.h"
#include "vm/gc.h"
#include "vm/vm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

static object_t *key(const gchar_t *text, object_static_string_t *storage) {
    return get_static_string_object(storage, text);
}

static void execute_file(process_t *process, module_files_t *files, module_file_t *file) {
    thread_t *thread = process->main_thread;
    context_t *caller = thread->context;
    context_t *context = create_module_file_context(file, caller);
    assert(context && context->previous == caller);
    assert(context->data == module_file_scope(file));
    assert(context->module == module_files_identity(files));
    object_static_string_t hidden_storage = {0}, builtin_storage = {0};
    object_t *hidden = key(U"caller_only", &hidden_storage);
    object_t *builtin = key(U"println", &builtin_storage);
    assert(lookup_accessible_property(&context->access, context->data, hidden, false).status
           == MSTAT_PROPERTY_NOT_FOUND);
    assert(lookup_accessible_property(&context->access, context->data, builtin, false).status
           == MSTAT_OK);
    thread->context = context;
    thread->instr_id = module_file_entry(file);
    assert(!run(process, module_files_bytecode(files)));
    assert(thread->data_stack->size == 0);
    assert(destroy_context(context) == caller);
    thread->context = caller;
}

int main(int argc, char **argv) {
    (void)argv;
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    process_t *process = create_process();
    module_files_t *files = (void *)(uintptr_t)1;
    module_bytecode_error_t error;
    if (argc == 1) {
        assert(create_module_files(process, NULL, 0, NULL, 0, &files, &error)
               == MODULE_BYTECODE_ARGUMENT);
        assert(!files);
        assert(create_module_files(NULL, "", 0, "", 0, &files, NULL) == MODULE_BYTECODE_ARGUMENT);
        assert(!files);
        assert(create_module_files(process, "", 0, "", 0, NULL, NULL) == MODULE_BYTECODE_ARGUMENT);
        destroy_process(process);
        assert(!get_allocated_memory_size());
        return 0;
    }
    size_t capacity = 32 * 1024 * 1024;
    unsigned char *input = malloc(capacity);
    assert(input);
    size_t size = FREAD(input, 1, capacity, stdin);
    assert(size >= 4 && size < capacity && !FERROR(stdin));
    size_t json_size = read_le32(input);
    assert(json_size <= size - 4);
    module_bytecode_status_t status = create_module_files(process,
                                                          (const char *)input + 4,
                                                          json_size,
                                                          input + 4 + json_size,
                                                          size - 4 - json_size,
                                                          &files,
                                                          &error);
    if (status) {
        assert(!files && error.status == status);
        free(input);
        destroy_process(process);
        assert(!get_allocated_memory_size());
        return status;
    }
    assert(files && module_files_count(files));
    yyjson_doc *document = yyjson_read((const char *)input + 4, json_size, 0);
    assert(document);
    yyjson_val *root = yyjson_doc_get_root(document);
    uint32_t main_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(root, "main"));
    module_files_t *other = NULL;
    assert(!create_module_files(process,
                                (const char *)input + 4,
                                json_size,
                                input + 4 + json_size,
                                size - 4 - json_size,
                                &other,
                                &error));
    assert(other != files && module_files_identity(other) != module_files_identity(files));
    bytecode_t *code = module_files_bytecode(files);
    size_t count = module_files_count(files);
    assert(count == code->file_count);
    assert(!module_files_get(files, (uint32_t)count));
    assert(!module_files_get(files, UINT32_MAX));
    unsigned char *copy = malloc(code->buffer_size);
    assert(copy);
    memcpy(copy, code->buffer, code->buffer_size);
    memset(input, 0xa5, size);
    free(input);

    object_static_string_t x_storage = {0}, hidden_storage = {0};
    object_t *x = key(U"x", &x_storage);
    object_t *hidden = key(U"caller_only", &hidden_storage);
    context_t *caller = process->main_thread->context;
    assert(create_accessible_property(&caller->access,
                                      caller->data,
                                      hidden,
                                      get_static_integer_object(7),
                                      false)
           == MSTAT_OK);
    assert(!module_files_count(NULL));
    assert(!module_files_get(NULL, 0));
    assert(!module_files_main(NULL));
    assert(!module_files_bytecode(NULL));
    assert(!module_files_identity(NULL));
    assert(!create_module_file_context(NULL, caller));
    assert(!create_module_file_context(module_files_main(files), NULL));
    process_t *foreign = create_process();
    assert(!create_module_file_context(module_files_main(files), foreign->main_thread->context));
    destroy_process(foreign);
    module_file_t *main = module_files_main(files);
    assert(module_file_id(main) == main_id);
    assert(main && module_file_id(main) < count);
    for (size_t i = 0; i < count; ++i) {
        module_file_t *file = module_files_get(files, (uint32_t)i);
        assert(file && module_file_id(file) == i);
        assert(module_file_entry(file) == code->files[i].entry);
        assert(module_file_scope(file) != module_file_scope(module_files_get(other, (uint32_t)i)));
        assert(get_object_module(module_file_scope(file)) == module_files_identity(files));
        assert(!get_own_field_cell(module_file_scope(file), x));
        assert(module_file_name(file).data && module_file_name(file).length);
        yyjson_val *description = yyjson_arr_get(yyjson_obj_get(root, "files"), i);
        yyjson_val *name = yyjson_obj_get(description, "name");
        string_value_t expected = decode_utf8_n(yyjson_get_str(name), yyjson_get_len(name));
        string_view_t actual = module_file_name(file);
        assert(expected.data && actual.length == expected.length);
        assert(!memcmp(actual.data, expected.data, actual.length * sizeof(gchar_t)));
        FREE_STRING(expected);
        assert(module_file_external(file)
               == yyjson_get_bool(yyjson_obj_get(description, "external")));
        assert(module_files_get(files, (uint32_t)i) == file);
        for (size_t j = 0; j < i; ++j)
            assert(module_file_scope(file)
                   != module_file_scope(module_files_get(files, (uint32_t)j)));
    }
    execute_file(process, files, main);
    /* Starting main never eagerly executes other files. */
    for (size_t i = 0; i < count; ++i) {
        module_file_t *file = module_files_get(files, (uint32_t)i);
        if (file != main) {
            assert(!get_own_field_cell(module_file_scope(file), x));
            execute_file(process, files, file);
        }
    }
    for (size_t i = 0; i < count; ++i) {
        module_file_t *file = module_files_get(files, (uint32_t)i);
        field_cell_t *cell = get_own_field_cell(module_file_scope(file), x);
        assert(cell);
        int_value_t value = get_object_integer_value(cell->object);
        assert(value.has_value
               && value.value == (int32_t)code->instructions[module_file_entry(file)].arg1);
        assert(set_field_cell_value(module_file_scope(file), cell, get_static_integer_object(9))
               == MSTAT_OK);
        context_t *again = create_module_file_context(file, caller);
        assert(again && again->data == module_file_scope(file));
        assert(get_object_integer_value(get_own_field_cell(again->data, x)->object).value == 9);
        destroy_context(again);
    }
    collect_garbage(process);
    for (size_t i = 0; i < count; ++i) {
        module_file_t *file = module_files_get(files, (uint32_t)i);
        assert(
            get_object_integer_value(get_own_field_cell(module_file_scope(file), x)->object).value
            == 9);
        assert(!get_own_field_cell(module_file_scope(module_files_get(other, (uint32_t)i)), x));
    }
    yyjson_doc_free(document);
    execute_file(process, other, module_files_main(other));
    assert(get_object_integer_value(get_own_field_cell(module_file_scope(main), x)->object).value
           == 9);
    assert(module_files_main(files) == main);
    assert(module_files_bytecode(files) == code);
    assert(!memcmp(copy, code->buffer, code->buffer_size));
    printf("%u %zu\n", module_file_id(main), module_file_entry(main));
    free(copy);
    destroy_process(process);
    assert(!get_allocated_memory_size());
    return 0;
}
