/** @file module_exception_propagation_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Exception propagation/finally rules 26--35 in the actual VM and file gate.
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
    unsigned rule;
    unsigned calls[3];
    object_t *original;
    object_t *replacement;
} test_t;

static object_t *key(const gchar_t *name, object_static_string_t *storage) {
    return get_static_string_object(storage, name);
}

static void clear_failure(exception_t *failure) {
    clear_exception(failure);
}

static bool run_frame(module_file_t *file, context_t *frame, exception_t *failure) {
    process_t *process = module_file_process(file);
    thread_t *thread = process->main_thread;
    context_t *previous = thread->context;
    instr_index_t address = thread->instr_id;
    object_t *sentinel = get_static_integer_object(31);
    INCREF(sentinel);
    size_t before = thread->data_stack->size;
    push_object_onto_stack(thread->data_stack, sentinel);
    thread->context = frame;
    thread->instr_id = module_file_entry(file);
    int status = run(process, module_file_bytecode(file));
    assert(thread->context == frame && thread->data_stack->size == before + 1);
    assert(pop_object_from_stack(thread->data_stack) == sentinel);
    DECREF(sentinel);
    thread->context = previous;
    thread->instr_id = address;
    if (status) {
        assert(thread->exception.value);
        *failure = thread->exception;
        thread->exception = (exception_t){0};
    } else {
        assert(!thread->exception.value);
    }
    return status == 0;
}

static bool execute(module_file_t *file, context_t *frame, void *data, exception_t *failure) {
    test_t *test = data;
    unsigned id = module_file_id(file);
    assert(id < 3 && ++test->calls[id] == 1);
    assert(module_file_state(file) == MODULE_FILE_INITIALIZING);
    bool nested = (id == 0
                   && (test->rule == 27 || test->rule == 28 || test->rule == 30 || test->rule == 32
                       || test->rule == 33))
                  || (id == 1 && (test->rule == 28 || test->rule == 32 || test->rule == 33));
    if (nested) {
        object_t *scope = NULL;
        exception_t child = {0};
        module_file_t *next = module_files_get(test->files, id + 1);
        module_initialize_status_t status =
            initialize_module_file(next, frame, execute, test, &scope, &child);
        if (test->rule == 30) {
            assert(status == MODULE_INITIALIZE_OK && !child.value);
            assert(scope == module_file_scope(next));
        } else {
            assert(status == MODULE_INITIALIZE_FAILED && !scope && child.value);
            if (test->rule == 32 || (test->rule == 28 && id == 1)
                || (test->rule == 33 && id == 0)) {
                *failure = child;
                return false;
            }
            assert(child.value == test->original);
            static object_static_string_t child_key = {0};
            assert(create_object_property(frame->data,
                                          key(U"child", &child_key),
                                          module_file_scope(next),
                                          false)
                   == MSTAT_OK);
            clear_failure(&child);
        }
    }
    return run_frame(file, frame, failure);
}

static int64_t own_integer(module_file_t *file, const gchar_t *name) {
    object_static_string_t storage = {0};
    field_cell_t *cell = get_own_field_cell(module_file_scope(file), key(name, &storage));
    assert(cell);
    int_value_t value = get_object_integer_value(cell->object);
    assert(value.has_value);
    return value.value;
}

int main(int argc, char **argv) {
    assert(argc == 3);
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    unsigned rule = (unsigned)atoi(argv[1]);
    assert(rule >= 26 && rule <= 35);
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
    unsigned char *original_code = malloc(code->buffer_size);
    assert(original_code);
    memcpy(original_code, code->buffer, code->buffer_size);
    object_t *root = get_root_object();
    object_t *original = !strcmp(argv[2], "null") ? get_null_object()
                         : !strcmp(argv[2], "integer")
                             ? create_integer_object(process, 7001)
                             : create_user_defined_object(process, (object_array_t){&root, 1});
    object_t *replacement = create_user_defined_object(process, (object_array_t){&root, 1});
    test_t test = {.files = files, .rule = rule, .original = original, .replacement = replacement};
    object_static_string_t ps = {0}, rs = {0}, cs = {0}, es = {0}, vs = {0};
    object_t *payload_key = key(U"payload", &ps), *replacement_key = key(U"replacement", &rs);
    object_t *cleanup_key = key(U"cleanup", &cs), *caught_key = key(U"caught", &es);
    object_t *value_key = key(U"value", &vs);
    for (unsigned i = 0; i < 3; ++i) {
        object_t *scope = module_file_scope(module_files_get(files, i));
        assert(create_object_property(scope, payload_key, original, false) == MSTAT_OK);
        assert(create_object_property(scope, replacement_key, replacement, false) == MSTAT_OK);
        assert(create_object_property(scope, cleanup_key, get_static_integer_object(0), false)
               == MSTAT_OK);
        assert(create_object_property(scope, caught_key, get_static_integer_object(0), false)
               == MSTAT_OK);
    }
    DECREF(original);
    DECREF(replacement);
    context_t *caller = process->main_thread->context;
    module_file_t *first = module_files_get(files, 0);
    exception_t failure = {0};
    object_t *scope = NULL;
    bool success = rule <= 29;
    module_initialize_status_t status =
        initialize_module_file(first, caller, execute, &test, &scope, &failure);
    assert(status == (success ? MODULE_INITIALIZE_OK : MODULE_INITIALIZE_FAILED));
    assert(process->main_thread->context == caller && !process->initializing_file);
    assert(module_file_state(first) == (success ? MODULE_FILE_INITIALIZED : MODULE_FILE_FAILED));
    object_t *expected = rule == 33 || rule == 34 ? replacement : original;
    assert(success ? scope == module_file_scope(first) && !failure.value
                   : !scope && failure.value == expected);
    const exception_trace_t *initial_trace = failure.trace;
    if (!success) {
        assert(initial_trace && exception_trace_size(initial_trace));
        unsigned origin_file = rule == 32 ? 2 : rule == 33 ? 1 : 0;
        unsigned offset = rule == 33 ? 11 : rule == 34 ? 8 : rule == 35 ? 2 : 1;
        const exception_trace_frame_t *origin = exception_trace_frame(initial_trace, 0);
        assert(origin && origin->module == module_files_identity(files));
        assert(origin->file_id == origin_file);
        assert(origin->instruction
               == module_file_entry(module_files_get(files, origin_file)) + offset);
        assert(!exception_trace_frame(initial_trace, exception_trace_size(initial_trace)));
    }
    clear_failure(&failure);
    if (rule <= 28)
        assert(own_integer(first, U"caught") == 1);
    if (rule == 33)
        assert(own_integer(module_files_get(files, 1), U"caught") == 1);
    if (rule >= 34)
        assert(own_integer(first, U"cleanup") == 1);
    if (rule == 27 || rule == 28 || rule == 32 || rule == 33) {
        assert(module_file_failure(module_files_get(files, 1))
               == (rule == 33 ? replacement : original));
    }
    if (rule == 28 || rule == 32 || rule == 33)
        assert(module_file_failure(module_files_get(files, 2)) == original);
    if (rule == 28 || rule == 32) {
        assert(module_file_exception(module_files_get(files, 1)).trace
               == module_file_exception(module_files_get(files, 2)).trace);
    }
    if (rule == 32)
        assert(module_file_exception(first).trace
               == module_file_exception(module_files_get(files, 2)).trace);
    if (rule == 33) {
        assert(module_file_exception(first).trace
               == module_file_exception(module_files_get(files, 1)).trace);
        assert(module_file_exception(first).trace
               != module_file_exception(module_files_get(files, 2)).trace);
    }
    if (rule == 30)
        assert(module_file_state(module_files_get(files, 1)) == MODULE_FILE_INITIALIZED);
    if (!strcmp(argv[2], "object")) {
        assert(create_object_property(original, value_key, get_static_integer_object(23), false)
               == MSTAT_OK);
    }
    collect_garbage(process);
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        for (unsigned i = 0; i < 3; ++i) {
            module_file_t *file = module_files_get(files, i);
            if (!test.calls[i])
                continue;
            module_file_state_t state = module_file_state(file);
            exception_t saved_record = module_file_exception(file);
            object_t *saved = saved_record.value;
            status = initialize_module_file(file, caller, execute, &test, &scope, &failure);
            assert(
                status
                == (state == MODULE_FILE_FAILED ? MODULE_INITIALIZE_FAILED : MODULE_INITIALIZE_OK));
            assert(failure.value == saved && failure.trace == saved_record.trace
                   && test.calls[i] == 1);
            if (saved && !strcmp(argv[2], "object") && saved == original)
                assert(get_object_integer_value(get_object_property(saved, value_key)).value == 23);
            clear_failure(&failure);
        }
    }
    if (rule >= 34)
        assert(own_integer(first, U"cleanup") == 1);
    if (rule == 29) {
        object_static_string_t fs = {0};
        object_t *function = get_object_property(module_file_scope(first), key(U"function", &fs));
        assert(function);
        thread_t *thread = process->main_thread;
        context_t *frame = create_module_file_context(first, caller);
        frame->file_initialization_boundary = true;
        thread->context = frame;
        thread->instr_id = module_file_entry(first);
        assert(call_object(function, 0, NULL, thread));
        assert(run(process, code) == 1);
        assert(thread->context == frame && thread->exception.value == original);
        assert(thread->exception.trace);
        const exception_trace_frame_t *origin = exception_trace_frame(thread->exception.trace, 0);
        assert(origin && origin->file_id == 0);
        assert(origin->instruction == module_file_entry(first) + 5);
        clear_failure(&thread->exception);
        thread->context = caller;
        destroy_context(frame);
        assert(module_file_state(first) == MODULE_FILE_INITIALIZED);
        assert(initialize_module_file(first, caller, execute, &test, &scope, &failure)
               == MODULE_INITIALIZE_OK);
        assert(!failure.value && test.calls[0] == 1 && scope == module_file_scope(first));
    }
    if (rule == 26) {
        context_t *frame = create_module_file_context(first, caller);
        frame->file_initialization_boundary = true;
        for (unsigned i = 0; i < 128; ++i)
            assert(run_frame(first, frame, &failure));
        size_t baseline = get_allocated_memory_size();
        for (unsigned i = 0; i < 256; ++i)
            assert(run_frame(first, frame, &failure));
        assert(get_allocated_memory_size() == baseline);
        destroy_context(frame);
    }
    assert(!memcmp(original_code, code->buffer, code->buffer_size));
    free(original_code);
    destroy_process(process);
    assert(!get_allocated_memory_size());
    puts("OK");
    return 0;
}
