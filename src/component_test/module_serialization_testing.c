/** @file module_serialization_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Deterministically coordinated OS-thread file initialization tests.
 */
#include "lib/allocate.h"
#include "lib/file_system.h"
#include "lib/os_thread.h"
#include "lib/wire.h"
#include "model/module_files.h"
#include "model/process.h"
#include "model/thread.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

typedef struct {
    process_t *process;
    module_files_t *files;
    os_mutex_t *mutex;
    os_condition_t *condition;
    bool entered, release, requested, failed, direct;
    unsigned active;
    unsigned calls[3];
    exception_t original;
} fixture_t;

typedef struct {
    fixture_t *test;
    unsigned file;
    bool second;
    object_t *scope;
    exception_t failure;
    module_initialize_status_t status;
} request_t;

static bool execute(module_file_t *file, context_t *frame, void *data, exception_t *failure) {
    fixture_t *test = data;
    unsigned id = module_file_id(file);
    assert(id < 3 && ++test->calls[id] == 1);
    if (id == 2)
        return true;
    assert(lock_os_mutex(test->mutex) == OS_THREAD_OK);
    assert(test->direct || !test->active);
    ++test->active;
    assert(unlock_os_mutex(test->mutex) == OS_THREAD_OK);
    object_static_string_t xs = {0}, cs = {0};
    object_t *x = get_static_string_object(&xs, U"x");
    object_t *c = get_static_string_object(&cs, U"c");
    assert(lookup_accessible_property(&frame->access, frame->data, x, false).status
           == MSTAT_UNINITIALIZED);
    assert(write_accessible_property(&frame->access, frame->data, x, get_static_integer_object(1))
           == MSTAT_UNINITIALIZED);
    /* Owner reentry returns exactly the existing partial scope, without executing twice. */
    object_t *recursive = NULL;
    exception_t recursive_failure = {0};
    assert(initialize_module_file(file, frame, execute, data, &recursive, &recursive_failure)
           == MODULE_INITIALIZE_OK);
    assert(recursive == frame->data && !recursive_failure.value);
    assert(initialize_field_cell(frame->data,
                                 get_own_field_cell(frame->data, c),
                                 get_static_integer_object(73))
           == MSTAT_OK);
    if (id == 0) {
        /* Simulates blocking I/O: keep initialization ownership, release only model lock. */
        size_t depth = process_execution_suspend(test->process);
        assert(depth);
        assert(lock_os_mutex(test->mutex) == OS_THREAD_OK);
        test->entered = true;
        assert(broadcast_os_condition(test->condition) == OS_THREAD_OK);
        while (!test->release)
            assert(wait_os_condition(test->condition, test->mutex) == OS_THREAD_OK);
        assert(unlock_os_mutex(test->mutex) == OS_THREAD_OK);
        process_execution_resume(test->process, depth);
    }
    assert(lock_os_mutex(test->mutex) == OS_THREAD_OK);
    --test->active;
    assert(unlock_os_mutex(test->mutex) == OS_THREAD_OK);
    if (test->failed) {
        *failure = retain_exception(test->original);
        return false;
    }
    assert(initialize_field_cell(frame->data,
                                 get_own_field_cell(frame->data, x),
                                 get_static_integer_object(42))
           == MSTAT_OK);
    return true;
}

static void *request(void *data) {
    request_t *r = data;
    fixture_t *test = r->test;
    if (r->second) {
        assert(lock_os_mutex(test->mutex) == OS_THREAD_OK);
        test->requested = true;
        assert(broadcast_os_condition(test->condition) == OS_THREAD_OK);
        assert(unlock_os_mutex(test->mutex) == OS_THREAD_OK);
    }
    module_file_t *file = module_files_get(test->files, r->file);
    context_t *caller = test->process->main_thread->context;
    r->status =
        test->direct && !r->second
            ? initialize_module_file_direct(file, caller, execute, test, &r->scope, &r->failure)
            : initialize_module_file(file, caller, execute, test, &r->scope, &r->failure);
    return NULL;
}

static void run_case(const unsigned char *input, size_t size, unsigned variant) {
    uint32_t json_size = read_le32(input);
    fixture_t test = {.process = create_process(), .failed = variant == 2, .direct = variant == 3};
    assert(create_os_mutex(&test.mutex) == OS_THREAD_OK);
    assert(create_os_condition(&test.condition) == OS_THREAD_OK);
    assert(create_module_files(test.process,
                               (const char *)input + 4,
                               json_size,
                               input + 4 + json_size,
                               size - 4 - json_size,
                               &test.files,
                               NULL)
           == MODULE_BYTECODE_OK);
    object_t *root = get_root_context()->data;
    test.original.value = create_user_defined_object(test.process, (object_array_t){&root, 1});
    test.original.trace = capture_exception_trace(test.process->main_thread->context, 0);
    assert(test.original.trace);
    object_t *ready = NULL;
    exception_t failure = {0};
    assert(initialize_module_file(module_files_get(test.files, 2),
                                  test.process->main_thread->context,
                                  execute,
                                  &test,
                                  &ready,
                                  &failure)
           == MODULE_INITIALIZE_OK);
    request_t a = {.test = &test},
              b = {.test = &test, .second = true, .file = variant == 1 ? 1 : 0};
    os_thread_t *first = NULL, *second = NULL;
    assert(create_os_thread(&first, request, &a, 0) == OS_THREAD_OK);
    assert(lock_os_mutex(test.mutex) == OS_THREAD_OK);
    while (!test.entered)
        assert(wait_os_condition(test.condition, test.mutex) == OS_THREAD_OK);
    assert(unlock_os_mutex(test.mutex) == OS_THREAD_OK);
    if (!test.direct) {
        assert(create_os_thread(&second, request, &b, 0) == OS_THREAD_OK);
        assert(lock_os_mutex(test.mutex) == OS_THREAD_OK);
        while (!test.requested)
            assert(wait_os_condition(test.condition, test.mutex) == OS_THREAD_OK);
        assert(unlock_os_mutex(test.mutex) == OS_THREAD_OK);
    }
    if (!test.direct) {
        process_execution_enter(test.process);
        while (!test.process->initialization_waiters)
            assert(wait_os_condition(test.process->threads_changed, test.process->execution_mutex)
                   == OS_THREAD_OK);
        process_execution_leave(test.process);
    }
    /* A ready independent file must not wait on the global initialization lock. */
    object_t *again = NULL;
    assert(initialize_module_file(module_files_get(test.files, 2),
                                  test.process->main_thread->context,
                                  execute,
                                  &test,
                                  &again,
                                  &failure)
           == MODULE_INITIALIZE_OK);
    assert(again == ready && test.calls[2] == 1);
    if (test.direct) {
        process_execution_enter(test.process);
        module_file_t *file = module_files_get(test.files, 0);
        context_t *frame = create_module_file_context(file, test.process->main_thread->context);
        object_static_string_t xs = {0}, cs = {0};
        object_t *x = get_static_string_object(&xs, U"x");
        object_t *c = get_static_string_object(&cs, U"c");
        assert(lookup_accessible_property(&frame->access, frame->data, x, false).status
               == MSTAT_UNINITIALIZED);
        assert(
            write_accessible_property(&frame->access, frame->data, x, get_static_integer_object(0))
            == MSTAT_UNINITIALIZED);
        operation_result_t value = read_accessible_property(&frame->access, frame->data, c);
        assert(!value.is_exception && get_object_integer_value(value.value).value == 73);
        DECREF(value.value);
        destroy_context(frame);
        process_execution_leave(test.process);
        object_t *independent = NULL;
        assert(initialize_module_file(module_files_get(test.files, 1),
                                      test.process->main_thread->context,
                                      execute,
                                      &test,
                                      &independent,
                                      &failure)
               == MODULE_INITIALIZE_OK);
        assert(independent);
    }
    assert(lock_os_mutex(test.mutex) == OS_THREAD_OK);
    assert(test.active);
    test.release = true;
    assert(broadcast_os_condition(test.condition) == OS_THREAD_OK);
    assert(unlock_os_mutex(test.mutex) == OS_THREAD_OK);
    assert(join_os_thread(first, NULL) == OS_THREAD_OK);
    assert(destroy_os_thread(first) == OS_THREAD_OK);
    if (second) {
        assert(join_os_thread(second, NULL) == OS_THREAD_OK);
        assert(destroy_os_thread(second) == OS_THREAD_OK);
    }
    assert(test.calls[0] == 1 && test.calls[1] == (variant == 1 || variant == 3 ? 1u : 0u));
    if (test.failed) {
        assert(a.status == MODULE_INITIALIZE_FAILED && b.status == MODULE_INITIALIZE_FAILED);
        assert(a.failure.value == test.original.value && b.failure.value == test.original.value);
        assert(a.failure.trace == test.original.trace && b.failure.trace == test.original.trace);
        clear_exception(&b.failure);
        request(&b);
        assert(test.calls[0] == 1 && b.failure.value == test.original.value);
    } else {
        assert(a.status == MODULE_INITIALIZE_OK);
        if (second)
            assert(b.status == MODULE_INITIALIZE_OK
                   && (variant == 1 ? b.scope != a.scope : b.scope == a.scope));
    }
    clear_exception(&a.failure);
    clear_exception(&b.failure);
    clear_exception(&test.original);
    assert(destroy_os_condition(test.condition) == OS_THREAD_OK);
    assert(destroy_os_mutex(test.mutex) == OS_THREAD_OK);
    destroy_process(test.process);
    assert(!get_allocated_memory_size());
}

int main(void) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    size_t capacity = 1024 * 1024;
    unsigned char *input = malloc(capacity);
    assert(input);
    size_t size = FREAD(input, 1, capacity, stdin);
    assert(size > 4 && size < capacity && !FERROR(stdin));
    assert(read_le32(input) < size - 4);
    for (unsigned variant = 0; variant < 4; ++variant)
        run_case(input, size, variant);
    free(input);
    puts("OK");
    return 0;
}
