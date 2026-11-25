/** @file gc_oom_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Isolated fatal allocation failures at managed and native boundaries.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "allocation_fault.h"
#include "codegen/linker.h"
#define GOAT_NATIVE_OK GOAT_MODULE_NATIVE_OK
#include "common/module_compilation.h"
#undef GOAT_NATIVE_OK
#include "lib/memory/allocate.h"
#include "lib/process/process_exit.h"
#include "lib/threads/os_thread.h"
#include "model/array.h"
#include "model/builtin_function.h"
#include "model/context.h"
#include "model/module_native.h"
#include "unit/analysis_test_support.h"
#include "vm/gc.h"
#include "vm/vm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#    include <io.h>
#    define LOCK_ERROR() _lock_file(stderr)
#    define UNLOCK_ERROR() _unlock_file(stderr)
#else
#    include <errno.h>
#    include <fcntl.h>
#    include <unistd.h>
#    define LOCK_ERROR() flockfile(stderr)
#    define UNLOCK_ERROR() funlockfile(stderr)
#endif
static os_mutex_t *gate;
static os_condition_t *changed;
static bool ready, release;

static void *hold_error(void *unused) {
    (void)unused;
    LOCK_ERROR();
    assert(lock_os_mutex(gate) == OS_THREAD_OK);
    ready = true;
    signal_os_condition(changed);
    while (!release)
        assert(wait_os_condition(changed, gate) == OS_THREAD_OK);
    unlock_os_mutex(gate);
    UNLOCK_ERROR();
    return NULL;
}

static size_t failure;

static void cleanup(void) {
    puts("ATEXIT");
    fflush(stdout);
}

static void arm(void) {
    puts("BEFORE");
    fflush(stdout);
    allocation_fault_start(failure);
}

static void allocation(void) {
    void *p = ALLOC(4096);
    FREE(p);
}

static operation_result_t host(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    (void)count;
    (void)thread;
    arm();
    allocation();
    allocation_fault_stop();
    return operation_success(get_null_object());
}

static uint32_t GOAT_MODULE_CALL native(uint32_t abi,
                                        const goat_module_context_v1 *context,
                                        uint32_t count,
                                        const goat_module_value_v1 *args,
                                        goat_module_value_v1 *result) {
    (void)context;
    (void)abi;
    assert(count == 1);
    arm();
    allocation();
    allocation_fault_stop();
    *result = args[0];
    return GOAT_MODULE_SUCCESS;
}

static bool resolve(void *state, uint32_t id, const char *symbol, module_native_entry_t *out) {
    (void)state;
    (void)symbol;
    assert(id == 1);
    *out = (module_native_entry_t){.adapter = native};
    return true;
}

static void execute(process_t *p, bool use_native) {
    const char *text =
        use_native
            ? "var bridge=func(x){return "
              "x;};try{bridge(42);}catch(e){println(\"CATCH\");}finally{println(\"FINALLY\");}"
            : "try{host();}catch(e){println(\"CATCH\");}finally{println(\"FINALLY\");}";
    module_source_t source = {.path = "main.goat",
                              .name = "main.goat",
                              .source = text,
                              .length = strlen(text)};
    module_sources_t sources = {.files = &source, .count = 1};
    module_compilation_t *compiled = NULL;
    module_compilation_error_t error = {0};
    assert(compile_module_sources(&sources, &compiled, &error));
    clear_module_compilation_error(&error);
    module_files_t *files = NULL;
    assert(create_module_files_from_snapshot(p, compiled->metadata, compiled->code, &files));
    module_file_t *file = module_files_main(files);
    if (use_native) {
        const char json[] = "{\"goatAbiVersion\":1,\"specializations\":[{\"functionId\":1,"
                            "\"specializationId\":1,\"symbol\":\"goat_fn_1_v1\","
                            "\"signature\":{\"id\":\"goat.adapter.v1\",\"arguments\":[\"integer\"],"
                            "\"result\":\"integer\"},"
                            "\"when\":{\"argumentTypes\":[\"integer\"]}}]}";
        yyjson_doc *variant = yyjson_read(json, sizeof(json) - 1, 0);
        assert(variant
               && bind_module_files_native(files,
                                           yyjson_doc_get_root(variant),
                                           false,
                                           resolve,
                                           NULL,
                                           NULL));
        yyjson_doc_free(variant);
    } else {
        static const builtin_function_t descriptor = {.name = U"host", .execute = host};
        static builtin_function_object_t storage;
        static object_static_string_t key;
        assert(create_object_property(module_file_scope(file),
                                      get_static_string_object(&key, U"host"),
                                      get_builtin_function_object(&storage, &descriptor),
                                      true)
               == MSTAT_OK);
    }
    assert(prepare_process_launch(p,
                                  module_files_identity(files),
                                  (uint32_t)module_file_id(file),
                                  "/oom/main.goat"));
    process_execution_enter(p);
    assert(configure_gc_policy(p, GC_POLICY_DEFAULT_MIN_BYTES, true));
    process_execution_leave(p);
    assert(run_module_file(p, file) == 0);
    free_module_compilation(compiled);
}

int main(int argc, char **argv) {
    assert(argc == 3);
    failure = (size_t)strtoul(argv[2], NULL, 10);
#ifdef GOAT_TEST_PROCESS_EXIT_FAILURE
    assert(!process_exit_prepare());
#else
    assert(process_exit_prepare());
#endif
    assert(!atexit(cleanup));
#ifndef _WIN32
    if (!strcmp(argv[1], "full-stderr")) {
        int descriptors[2];
        assert(!pipe(descriptors));
        int flags = fcntl(descriptors[1], F_GETFL);
        assert(flags >= 0 && !fcntl(descriptors[1], F_SETFL, flags | O_NONBLOCK));
        char fill[4096] = {0};
        while (write(descriptors[1], fill, sizeof(fill)) > 0) {
        }
        assert(errno == EAGAIN);
        assert(!fcntl(descriptors[1], F_SETFL, flags));
        assert(dup2(descriptors[1], STDERR_FILENO) == STDERR_FILENO);
        close(descriptors[1]); // keep the unread reader open until process death
    }
#endif
    os_thread_t *holder = NULL;
    if (!strcmp(argv[1], "locked-stderr")) {
        assert(create_os_mutex(&gate) == OS_THREAD_OK);
        assert(create_os_condition(&changed) == OS_THREAD_OK);
        assert(lock_os_mutex(gate) == OS_THREAD_OK);
        assert(create_os_thread(&holder, hold_error, NULL, 0) == OS_THREAD_OK);
        while (!ready)
            assert(wait_os_condition(changed, gate) == OS_THREAD_OK);
        unlock_os_mutex(gate);
    }
    process_t *p = create_process();
    if (!strcmp(argv[1], "host") || !strcmp(argv[1], "native")) {
        execute(p, !strcmp(argv[1], "native"));
    } else {
        object_t *array = create_array_object(p, (object_array_t){0});
        process_execution_enter(p);
        if (!strcmp(argv[1], "worklist")) {
            operation_result_t r = insert_array_elements(array, 0, &array, 1);
            assert(!r.is_exception);
            DECREF(r.value);
            DECREF(array);
            arm();
            collect_garbage(p);
            assert(allocation_fault_stop() == 0 && !allocation_fault_triggered());
        } else if (!strcmp(argv[1], "roots")) {
            arm();
            gc_root_handle_t root = register_gc_root(p, array);
            allocation_fault_stop();
            assert(root.serial && unregister_gc_root(p, root));
            DECREF(array);
        } else if (!strcmp(argv[1], "array")) {
            object_t *item = get_null_object();
            arm();
            operation_result_t r = insert_array_elements(array, 0, &item, 1);
            allocation_fault_stop();
            assert(!r.is_exception);
            DECREF(r.value);
            DECREF(array);
        } else {
            assert(!strcmp(argv[1], "host-alloc") || !strcmp(argv[1], "calloc")
                   || !strcmp(argv[1], "full-stderr") || holder);
            arm();
            if (!strcmp(argv[1], "calloc")) {
                void *zero = CALLOC(4096);
                FREE(zero);
            } else
                allocation();
            allocation_fault_stop();
            DECREF(array);
        }
        process_execution_leave(p);
    }
    destroy_process(p);
    if (holder) {
        lock_os_mutex(gate);
        release = true;
        signal_os_condition(changed);
        unlock_os_mutex(gate);
        assert(join_os_thread(holder, NULL) == OS_THREAD_OK);
        destroy_os_thread(holder);
        destroy_os_condition(changed);
        destroy_os_mutex(gate);
    }
    puts("AFTER");
    return 0;
}
