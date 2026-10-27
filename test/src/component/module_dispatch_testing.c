/** @file module_dispatch_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Production VM dispatch with controlled module-v1 adapters, without OS loading.
 */
#define GOAT_NATIVE_OK GOAT_MODULE_NATIVE_OK
#include "common/module_compilation.h"
#undef GOAT_NATIVE_OK
#include "lib/allocate.h"
#include "lib/binary_file.h"
#include "lib/file_system.h"
#include "lib/io.h"
#include "lib/process_channel.h"
#include "model/module_native.h"
#include "model/native_library.h"
#include "model/native_services.h"
#include "vm/vm.h"

#include <assert.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_module_dispatch_identity(void);

static atomic_uint calls;
static unsigned resolutions, cleanups;
static atomic_bool native_entered;
static os_mutex_t *block_mutex;
static os_condition_t *block_condition;
static bool fail_resolution;
static yyjson_val *actions, *specializations;

static uint32_t GOAT_MODULE_CALL first(uint32_t abi,
                                       const goat_module_context_v1 *context,
                                       uint32_t count,
                                       const goat_module_value_v1 *args,
                                       goat_module_value_v1 *result) {
    (void)context;
    assert(abi == GOAT_MODULE_ABI_VERSION && count);
    ++calls;
    *result = args[0];
    return GOAT_MODULE_SUCCESS;
}

static uint32_t GOAT_MODULE_CALL second(uint32_t abi,
                                        const goat_module_context_v1 *context,
                                        uint32_t count,
                                        const goat_module_value_v1 *args,
                                        goat_module_value_v1 *result) {
    (void)context;
    assert(abi == GOAT_MODULE_ABI_VERSION && count == 2);
    ++calls;
    *result = args[1];
    return GOAT_MODULE_SUCCESS;
}

static uint32_t GOAT_MODULE_CALL sum(uint32_t abi,
                                     const goat_module_context_v1 *context,
                                     uint32_t count,
                                     const goat_module_value_v1 *args,
                                     goat_module_value_v1 *result) {
    (void)context;
    assert(abi == GOAT_MODULE_ABI_VERSION && count == 2);
    ++calls;
    result->tag = GOAT_MODULE_INTEGER;
    result->payload = args[0].payload + args[1].payload;
    return GOAT_MODULE_SUCCESS;
}

#define FIXED_ADAPTER(name, tag_value, reserved_value, payload_value, status_value)                \
    static uint32_t GOAT_MODULE_CALL name(uint32_t abi,                                            \
                                          const goat_module_context_v1 *context,                   \
                                          uint32_t count,                                          \
                                          const goat_module_value_v1 *args,                        \
                                          goat_module_value_v1 *result) {                          \
        (void)context;                                                                             \
        (void)count;                                                                               \
        (void)args;                                                                                \
        assert(abi == GOAT_MODULE_ABI_VERSION);                                                    \
        ++calls;                                                                                   \
        *result = (goat_module_value_v1){tag_value, reserved_value, payload_value};                \
        return status_value;                                                                       \
    }

FIXED_ADAPTER(negative, GOAT_MODULE_INTEGER, 0, (uint64_t)-100, GOAT_MODULE_SUCCESS)
FIXED_ADAPTER(positive, GOAT_MODULE_INTEGER, 0, 200, GOAT_MODULE_SUCCESS)
FIXED_ADAPTER(bad_tag, 99, 0, 0, GOAT_MODULE_SUCCESS)
FIXED_ADAPTER(bad_reserved, GOAT_MODULE_INTEGER, 1, 7, GOAT_MODULE_SUCCESS)
FIXED_ADAPTER(bad_bool, GOAT_MODULE_BOOLEAN, 0, 2, GOAT_MODULE_SUCCESS)
FIXED_ADAPTER(bad_char, GOAT_MODULE_CHARACTER, 0, 0xd800, GOAT_MODULE_SUCCESS)
FIXED_ADAPTER(bad_string, GOAT_MODULE_STRING, 0, 8, GOAT_MODULE_SUCCESS)
FIXED_ADAPTER(status_mismatch, GOAT_MODULE_INTEGER, 0, 7, GOAT_MODULE_TYPE_MISMATCH)
FIXED_ADAPTER(status_resource, GOAT_MODULE_INTEGER, 0, 7, GOAT_MODULE_RESOURCE_LIMIT)
FIXED_ADAPTER(status_unknown, GOAT_MODULE_INTEGER, 0, 7, UINT32_MAX)

static uint32_t GOAT_MODULE_CALL allocated_string(uint32_t abi,
                                                  const goat_module_context_v1 *context,
                                                  uint32_t count,
                                                  const goat_module_value_v1 *args,
                                                  goat_module_value_v1 *result) {
    (void)args;
    assert(abi == GOAT_MODULE_ABI_VERSION && !count);
    ++calls;
    goat_module_string_v1 *view = context->services->allocate(context->state, sizeof(*view));
    memset(view, 0, sizeof(*view));
    uint32_t *data = context->services->allocate(context->state, 3 * sizeof(*data));
    data[0] = 'A';
    data[1] = 0;
    data[2] = 0x1f600;
    view->data = data;
    view->length = 3;
    result->tag = GOAT_MODULE_STRING;
    result->payload = (uint64_t)(uintptr_t)view;
    return GOAT_MODULE_SUCCESS;
}

/** Native host-service calls, without any independent C/OS termination path. */
static uint32_t GOAT_MODULE_CALL native_exit(uint32_t abi,
                                             const goat_module_context_v1 *context,
                                             uint32_t count,
                                             const goat_module_value_v1 *args,
                                             goat_module_value_v1 *result) {
    (void)result;
    assert(abi == GOAT_MODULE_ABI_VERSION);
    ++calls;
    return goat_module_request_exit(context,
                                    count,
                                    count ? args[0].tag : 0,
                                    count ? (int64_t)args[0].payload : 0);
}

static uint32_t GOAT_MODULE_CALL native_exit_ignored(uint32_t abi,
                                                     const goat_module_context_v1 *context,
                                                     uint32_t count,
                                                     const goat_module_value_v1 *args,
                                                     goat_module_value_v1 *result) {
    (void)native_exit(abi, context, count, args, result);
    *result = (goat_module_value_v1){GOAT_MODULE_INTEGER, 0, 0};
    return GOAT_MODULE_SUCCESS;
}

FIXED_ADAPTER(forged_exception, GOAT_MODULE_INTEGER, 0, 0, GOAT_MODULE_HOST_EXCEPTION)

static uint32_t GOAT_MODULE_CALL native_loop(uint32_t abi,
                                             const goat_module_context_v1 *context,
                                             uint32_t count,
                                             const goat_module_value_v1 *args,
                                             goat_module_value_v1 *result) {
    (void)abi;
    (void)context;
    (void)count;
    (void)args;
    (void)result;
    atomic_store_explicit(&native_entered, true, memory_order_release);
    for (;;) { /* Deliberately noncooperative native code. */
    }
}

static uint32_t GOAT_MODULE_CALL native_block(uint32_t abi,
                                              const goat_module_context_v1 *context,
                                              uint32_t count,
                                              const goat_module_value_v1 *args,
                                              goat_module_value_v1 *result) {
    (void)abi;
    (void)context;
    (void)count;
    (void)args;
    (void)result;
    assert(lock_os_mutex(block_mutex) == OS_THREAD_OK);
    atomic_store_explicit(&native_entered, true, memory_order_release);
    for (;;)
        assert(wait_os_condition(block_condition, block_mutex) == OS_THREAD_OK);
}

static uint32_t GOAT_MODULE_CALL native_wait(uint32_t abi,
                                             const goat_module_context_v1 *context,
                                             uint32_t count,
                                             const goat_module_value_v1 *args,
                                             goat_module_value_v1 *result) {
    (void)abi;
    (void)context;
    (void)count;
    (void)args;
    while (!atomic_load_explicit(&native_entered, memory_order_acquire)) {
    }
    *result = (goat_module_value_v1){GOAT_MODULE_INTEGER, 0, 0};
    return GOAT_MODULE_SUCCESS;
}

/** Exact optional-prefix boundaries; no extension access with an old provider. */
static void exit_service_prefixes(void) {
    goat_module_services_v1 module = {0};
    goat_module_context_v1 module_context = {&module, NULL};
    module.struct_size = GOAT_MODULE_SERVICES_SIZE;
    assert(goat_module_request_exit(&module_context, 1, GOAT_MODULE_INTEGER, 7)
           == GOAT_MODULE_ABI_MISMATCH);
    module.struct_size = GOAT_MODULE_EXIT_SERVICES_SIZE - 1;
    assert(goat_module_request_exit(&module_context, 1, GOAT_MODULE_INTEGER, 7)
           == GOAT_MODULE_ABI_MISMATCH);
    module.struct_size = GOAT_MODULE_EXIT_SERVICES_SIZE;
    assert(goat_module_request_exit(&module_context, 1, GOAT_MODULE_INTEGER, 7)
           == GOAT_MODULE_ABI_MISMATCH);
    goat_native_services_v2_t legacy = {0};
    goat_native_context_v2_t legacy_context = {&legacy, NULL};
    legacy.struct_size = GOAT_NATIVE_STRING_SERVICES_SIZE;
    assert(goat_native_request_exit(&legacy_context, 1, GOAT_NATIVE_I64, 7)
           == GOAT_NATIVE_ABI_MISMATCH);
    legacy.struct_size = GOAT_NATIVE_EXIT_SERVICES_SIZE - 1;
    assert(goat_native_request_exit(&legacy_context, 1, GOAT_NATIVE_I64, 7)
           == GOAT_NATIVE_ABI_MISMATCH);
    legacy.struct_size = GOAT_NATIVE_EXIT_SERVICES_SIZE;
    assert(goat_native_request_exit(&legacy_context, 1, GOAT_NATIVE_I64, 7)
           == GOAT_NATIVE_ABI_MISMATCH);
    native_memory_t standalone = {0};
    legacy_context = init_native_context(&standalone);
    assert(goat_native_request_exit(&legacy_context, 1, GOAT_NATIVE_I64, 7)
           == GOAT_NATIVE_BAD_REQUEST);
}

static goat_module_adapter_v1 lookup(const char *action) {
    struct {
        const char *name;
        goat_module_adapter_v1 adapter;
    } entries[] = {{"first", first},
                   {"second", second},
                   {"sum", sum},
                   {"negative", negative},
                   {"positive", positive},
                   {"bad-tag", bad_tag},
                   {"bad-reserved", bad_reserved},
                   {"bad-bool", bad_bool},
                   {"bad-char", bad_char},
                   {"bad-string", bad_string},
                   {"status-mismatch", status_mismatch},
                   {"status-resource", status_resource},
                   {"status-unknown", status_unknown},
                   {"string", allocated_string},
                   {"exit", native_exit},
                   {"exit-ignored", native_exit_ignored},
                   {"exit-forged", forged_exception},
                   {"loop", native_loop},
                   {"block", native_block},
                   {"wait", native_wait}};

    for (size_t i = 0; i < sizeof(entries) / sizeof(*entries); ++i)
        if (!strcmp(action, entries[i].name))
            return entries[i].adapter;
    assert(false);
    return NULL;
}

static bool
resolve(void *state, uint32_t specialization_id, const char *symbol, module_native_entry_t *out) {
    assert(state == &resolutions);
    ++resolutions;
    if (fail_resolution)
        return false;
    size_t index, maximum;
    yyjson_val *spec;
    yyjson_arr_foreach(specializations, index, maximum, spec) {
        if (yyjson_get_uint(yyjson_obj_get(spec, "specializationId")) == specialization_id) {
            assert(!strcmp(symbol, yyjson_get_str(yyjson_obj_get(spec, "symbol"))));
            out->adapter = lookup(yyjson_get_str(yyjson_arr_get(actions, index)));
            return true;
        }
    }
    assert(false);
    return false;
}

static void cleanup(void *state) {
    assert(state == &resolutions);
    ++cleanups;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--exit-prefixes")) {
        exit_service_prefixes();
        assert(!get_allocated_memory_size());
        puts("OK");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--identity")) {
        test_module_dispatch_identity();
        assert(!get_allocated_memory_size());
        puts("OK");
        return 0;
    }
    assert(create_os_mutex(&block_mutex) == OS_THREAD_OK);
    assert(create_os_condition(&block_condition) == OS_THREAD_OK);
    fs_error_t argument_error = {0};
    assert(fs_arguments(&argc, &argv, &argument_error));
    assert(argc == 4);
    module_sources_t *sources = NULL;
    module_sources_error_t source_error = {0};
    module_compilation_t *compilation = NULL;
    module_compilation_error_t compile_error = {0};
    assert(!module_sources_read(argv[1], false, NULL, &sources, &source_error));
    if (!compile_module_sources(sources, &compilation, &compile_error)) {
        fprintf(stderr, "Compilation: %s\n", compile_error.message);
        return 2;
    }
    size_t config_size = 0;
    void *config_data = read_binary_file(argv[2], 1024 * 1024, &config_size);
    assert(config_data);
    yyjson_doc *configuration = yyjson_read(config_data, config_size, 0);
    FREE(config_data);
    assert(configuration);
    yyjson_val *root = yyjson_doc_get_root(configuration);
    specializations = yyjson_obj_get(root, "specializations");
    actions = yyjson_obj_get(root, "actions");
    fail_resolution = yyjson_get_bool(yyjson_obj_get(root, "resolverFailure"));
    process_t *process = create_process();
    module_files_t *files = NULL;
    assert(create_module_files_from_snapshot(process,
                                             compilation->metadata,
                                             compilation->code,
                                             &files));
    size_t main_file = module_file_id(module_files_main(files));
    assert(prepare_process_launch(process,
                                  module_files_identity(files),
                                  (uint32_t)main_file,
                                  sources->files[main_file].path));
    const char *legacy_path = yyjson_get_str(yyjson_obj_get(root, "legacyLibrary"));
    native_library_result_t legacy = {0};
    if (legacy_path) {
        legacy = load_native_library(legacy_path);
        assert(legacy.status == NATIVE_LIBRARY_OK);
        native_function_descriptor_t *function =
            create_native_function_descriptor(legacy.library, 1);
        assert(function);
        bytecode_t *code = module_files_bytecode(files);
        for (size_t i = 0; i < code->instructions_count; ++i) {
            if (code->instructions[i].opcode == FUNC) {
                assert(bind_bytecode_native_function(code, (instr_index_t)i, function));
                break;
            }
        }
        release_native_function_descriptor(function);
    }
    bool bound = !legacy_path && strcmp(argv[3], "off") && yyjson_arr_size(specializations);
    bool binding_failure = yyjson_get_bool(yyjson_obj_get(root, "bindingFailure"));
    if (bound) {
        yyjson_mut_doc *variant = yyjson_mut_doc_new(NULL);
        yyjson_mut_val *object = yyjson_mut_obj(variant);
        yyjson_mut_doc_set_root(variant, object);
        yyjson_mut_obj_add_uint(variant, object, "goatAbiVersion", 1);
        yyjson_mut_obj_add_val(variant,
                               object,
                               "specializations",
                               yyjson_val_mut_copy(variant, specializations));
        yyjson_doc *immutable = yyjson_mut_doc_imut_copy(variant, NULL);
        assert(immutable);
        bool accepted = bind_module_files_native(files,
                                                 yyjson_doc_get_root(immutable),
                                                 !strcmp(argv[3], "required"),
                                                 resolve,
                                                 &resolutions,
                                                 cleanup);
        assert(accepted != binding_failure);
        bound = accepted;
        /* Dispatch must own metadata, not retain this short-lived input document. */
        yyjson_doc_free(immutable);
        yyjson_mut_doc_free(variant);
    }
    int result = binding_failure ? 0 : run_module_file(process, module_files_main(files));
    thread_t *thread = process->main_thread;
    size_t attempts = 0, successes = 0, retries = 0;
    do {
        attempts += thread->native_attempts;
        successes += thread->native_successes;
        retries += thread->native_retries;
        if (thread->exception.value) {
            string_value_t error = convert_object_to_string(thread->exception.value);
            fprintf_utf8(stderr, U"VM error: %s\n", error.data);
            FREE_STRING(error);
        }
        if (thread->native_diagnostic) {
            fprintf(stderr, "%s\n", thread->native_diagnostic);
            const exception_trace_frame_t *origin =
                exception_trace_frame(thread->exception.trace, 0);
            assert(origin && origin->native_entry);
            string_value_t trace = format_exception_trace(process, thread->exception.trace);
            fprintf_utf8(stderr, U"%s\n", trace.data);
            FREE_STRING(trace);
        }
        thread = thread->next;
    } while (thread != process->main_thread);
    fprintf(
        stderr,
        "METRICS "
        "{\"calls\":%u,\"resolutions\":%u,\"attempts\":%zu,\"successes\":%zu,\"retries\":%zu}\n",
        calls,
        resolutions,
        attempts,
        successes,
        retries);
    if (process->native_failed) {
        unsigned before_calls = calls, before_resolutions = resolutions;
        assert(run_module_file(process, module_files_main(files)) != 0);
        assert(calls == before_calls && resolutions == before_resolutions);
    }
    destroy_process(process);
    destroy_native_library_result(&legacy);
    assert(cleanups == (unsigned)bound);
    yyjson_doc_free(configuration);
    free_module_compilation(compilation);
    clear_module_compilation_error(&compile_error);
    free_module_sources(sources);
    clear_module_sources_error(&source_error);
    free_fs_arguments(argc, argv);
    assert(destroy_os_condition(block_condition) == OS_THREAD_OK);
    assert(destroy_os_mutex(block_mutex) == OS_THREAD_OK);
    assert(!get_allocated_memory_size());
    return result;
}
