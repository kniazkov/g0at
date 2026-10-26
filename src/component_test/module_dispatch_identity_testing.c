/** @file module_dispatch_identity_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Module-local native IDs stay distinct across instances, names and versions.
 */
#define GOAT_NATIVE_OK GOAT_MODULE_NATIVE_OK
#include "common/module_compilation.h"
#undef GOAT_NATIVE_OK
#include "lib/allocate.h"
#include "model/module_native.h"
#include "module/module_hash.h"
#include "vm/vm.h"

#include <assert.h>
#include <string.h>

static unsigned identity_calls[3], identity_resolutions[3], identity_cleanups[3];
static bool identity_failure;

#define IDENTITY_ADAPTER(name, index)                                                              \
    static uint32_t GOAT_MODULE_CALL name(uint32_t abi,                                            \
                                          const goat_module_context_v1 *context,                   \
                                          uint32_t count,                                          \
                                          const goat_module_value_v1 *arguments,                   \
                                          goat_module_value_v1 *result) {                          \
        (void)context;                                                                             \
        assert(abi == GOAT_MODULE_ABI_VERSION && count == 1);                                      \
        assert(arguments[0].tag == GOAT_MODULE_INTEGER);                                           \
        ++identity_calls[index];                                                                   \
        if (identity_failure) {                                                                    \
            *result = (goat_module_value_v1){99, 0, 0};                                            \
            return GOAT_MODULE_SUCCESS;                                                            \
        }                                                                                          \
        *result = (goat_module_value_v1){GOAT_MODULE_INTEGER,                                      \
                                         0,                                                        \
                                         arguments[0].payload + 100 * ((index) + 1)};              \
        return GOAT_MODULE_SUCCESS;                                                                \
    }

IDENTITY_ADAPTER(identity_first, 0)
IDENTITY_ADAPTER(identity_second, 1)
IDENTITY_ADAPTER(identity_third, 2)

static bool resolve_identity(void *state,
                             uint32_t specialization,
                             const char *symbol,
                             module_native_entry_t *out) {
    size_t index = *(size_t *)state;
    assert(index < 3 && specialization == 1 && !strcmp(symbol, "goat_fn_1_v1"));
    ++identity_resolutions[index];
    goat_module_adapter_v1 adapters[] = {identity_first, identity_second, identity_third};
    *out = (module_native_entry_t){.adapter = adapters[index]};
    return true;
}

static void cleanup_identity(void *state) {
    size_t index = *(size_t *)state;
    assert(index < 3);
    ++identity_cleanups[index];
}

/** Invoked by the component driver, separately from its configurable call fixtures. */
void test_module_dispatch_identity(void) {
    char source[] = "external f = func(x) { return x; }; external result = f(7);";
    module_source_t file = {.path = "identity.goat",
                            .name = "identity.goat",
                            .source = source,
                            .length = sizeof(source) - 1};
    module_sources_t sources = {.files = &file, .count = 1};
    module_compilation_t *compilation = NULL;
    module_compilation_error_t error = {0};
    assert(compile_module_sources(&sources, &compilation, &error));
    assert(compilation->code->function_count == 1 && compilation->code->functions[0].id == 1);
    const char variant_text[] =
        "{\"goatAbiVersion\":1,\"specializations\":[{\"functionId\":1,"
        "\"specializationId\":1,\"symbol\":\"goat_fn_1_v1\","
        "\"signature\":{\"id\":\"goat.adapter.v1\",\"arguments\":[\"integer\"],"
        "\"result\":\"integer\"},\"when\":{\"argumentTypes\":[\"integer\"]}}]}";
    yyjson_doc *variant = yyjson_read(variant_text, sizeof(variant_text) - 1, 0);
    assert(variant);
    process_t *process = create_process();
    module_files_t *modules[3];
    size_t states[] = {0, 1, 2};
    const char *names[] = {"owner", "other", "owner"};
    const char *versions[] = {"1", "1", "2"};
    for (size_t i = 0; i < 3; ++i) {
        yyjson_mut_doc *metadata = yyjson_doc_mut_copy(compilation->metadata, NULL);
        yyjson_mut_val *root = yyjson_mut_doc_get_root(metadata);
        assert(yyjson_mut_obj_put(root,
                                  yyjson_mut_str(metadata, "name"),
                                  yyjson_mut_str(metadata, names[i])));
        assert(yyjson_mut_obj_put(root,
                                  yyjson_mut_str(metadata, "version"),
                                  yyjson_mut_str(metadata, versions[i])));
        yyjson_doc *snapshot = yyjson_mut_doc_imut_copy(metadata, NULL);
        char hash[GOAT_MODULE_HASH_HEX_SIZE];
        assert(snapshot
               && !goat_module_main_hash(snapshot,
                                         compilation->code->buffer,
                                         compilation->code->buffer_size,
                                         hash));
        yyjson_doc_free(snapshot);
        assert(yyjson_mut_obj_put(root,
                                  yyjson_mut_str(metadata, "mainHash"),
                                  yyjson_mut_str(metadata, hash)));
        snapshot = yyjson_mut_doc_imut_copy(metadata, NULL);
        assert(snapshot
               && create_module_files_from_snapshot(process,
                                                    snapshot,
                                                    compilation->code,
                                                    &modules[i]));
        yyjson_doc_free(snapshot);
        yyjson_mut_doc_free(metadata);
        assert(bind_module_files_native(modules[i],
                                        yyjson_doc_get_root(variant),
                                        true,
                                        resolve_identity,
                                        &states[i],
                                        cleanup_identity));
        for (size_t j = 0; j < i; ++j)
            assert(module_files_identity(modules[i]) != module_files_identity(modules[j]));
    }
    yyjson_doc_free(variant);
    object_static_string_t result_key = {0}, function_key = {0};
    object_t *result_name = get_static_string_object(&result_key, U"result");
    object_t *function_name = get_static_string_object(&function_key, U"f");
    /* Every instance contains identical function ID, instruction and bytes, yet resolves its
     * own adapter. Their metadata inputs are already gone before executing anything. */
    for (size_t i = 0; i < 3; ++i) {
        assert(!run_module_file(process, module_files_main(modules[i])));
        field_cell_t *result =
            get_own_field_cell(module_file_scope(module_files_main(modules[i])), result_name);
        assert(result
               && get_object_integer_value(result->object).value == (int64_t)(107 + 100 * i));
    }
    process_execution_enter(process);
    thread_t *thread = process->main_thread;
    /* Interleave escaped callables after all modules ran. Numeric IDs or the most recently
     * initialized module must never select a foreign adapter. */
    const size_t order[] = {2, 0, 1, 0, 2, 1};
    for (size_t k = 0; k < sizeof(order) / sizeof(*order); ++k) {
        size_t i = order[k];
        field_cell_t *function =
            get_own_field_cell(module_file_scope(module_files_main(modules[i])), function_name);
        assert(function);
        context_t *caller = thread->context;
        size_t stack_size = thread->data_stack->size;
        push_object_onto_stack(thread->data_stack, get_static_integer_object(9));
        assert(call_object(function->object, 1, NULL, thread));
        assert(thread->context == caller && thread->data_stack->size == stack_size + 1);
        object_t *value = pop_object_from_stack(thread->data_stack);
        assert(get_object_integer_value(value).value == (int64_t)(109 + 100 * i));
        DECREF(value);
    }
    process_execution_leave(process);
    for (size_t i = 0; i < 3; ++i)
        assert(identity_calls[i] == 3 && identity_resolutions[i] && !identity_cleanups[i]);
    /* A backend contract failure is terminal even for an embedder attempting to reenter
     * an escaped function directly, or restart a completed module in the same process. */
    identity_failure = true;
    process_execution_enter(process);
    size_t stack_size = thread->data_stack->size;
    for (size_t i = 0; i < 3; ++i) {
        field_cell_t *function =
            get_own_field_cell(module_file_scope(module_files_main(modules[i])), function_name);
        push_object_onto_stack(thread->data_stack, get_static_integer_object(9));
        assert(!call_object(function->object, 1, NULL, thread));
        reduce_object_stack(thread->data_stack, stack_size);
        assert(process->native_failed);
    }
    assert(identity_calls[0] == 4 && identity_calls[1] == 3 && identity_calls[2] == 3);
    assert(thread->native_diagnostic && strstr(thread->native_diagnostic, "owner@1")
           && strstr(thread->native_diagnostic, "function 1"));
    process_execution_leave(process);
    assert(run_module_file(process, module_files_main(modules[0])) != 0);
    assert(identity_calls[0] == 4 && identity_calls[1] == 3 && identity_calls[2] == 3);
    destroy_process(process);
    for (size_t i = 0; i < 3; ++i)
        assert(identity_cleanups[i] == 1);
    free_module_compilation(compilation);
    clear_module_compilation_error(&error);
    assert(!get_allocated_memory_size());
}
