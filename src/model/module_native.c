/** @file module_native.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Module-local, side-effect-free native selection and single-entry ABI boundary.
 */
#include "module_native.h"

#include "lib/alignment.h"
#include "lib/allocate.h"
#include "lib/file_system.h"
#include "module/module_guards.h"
#include "module/module_json_internal.h"
#include "native_library.h"
#include "object.h"
#include "process.h"

#include <goat/unicode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct module_native_binding_t {
    yyjson_doc *metadata;
    bool required, unavailable;
    module_native_resolver_t resolve;
    void *state;
    void (*cleanup)(void *);
};

typedef struct call_block_t {
    _Alignas(memory_alignment_t) struct call_block_t *next;
    size_t size;
} call_block_t;

typedef struct {
    call_block_t *blocks;
    const goat_module_string_v1 *strings;
    size_t count;
    module_native_entry_t entry;
} call_memory_t;

static void *GOAT_MODULE_CALL allocate_memory(void *state, uint64_t size) {
    if (size > SIZE_MAX)
        allocation_size_add(SIZE_MAX, 1);
    call_memory_t *memory = state;
    size_t length = size ? (size_t)size : 1;
    call_block_t *block = ALLOC(allocation_size_add(sizeof(*block), length));
    block->next = memory->blocks;
    block->size = length;
    memory->blocks = block;
    return block + 1;
}

static void GOAT_MODULE_CALL deallocate_memory(void *state, void *pointer) {
    if (!pointer)
        return;
    call_memory_t *memory = state;
    call_block_t **link = &memory->blocks;
    while (*link && (void *)(*link + 1) != pointer)
        link = &(*link)->next;
    if (*link) {
        call_block_t *block = *link;
        *link = block->next;
        FREE(block);
    }
}

static void GOAT_MODULE_CALL deallocate_all(void *state) {
    call_memory_t *memory = state;
    while (memory->blocks)
        deallocate_memory(memory, memory->blocks + 1);
}

static uint32_t GOAT_MODULE_CALL is_space(uint32_t value) {
    return ISSPACE(value) ? 1 : 0;
}

static uint32_t GOAT_MODULE_CALL to_upper(uint32_t value) {
    return (uint32_t)TOUPPER(value);
}

static uint32_t GOAT_MODULE_CALL to_lower(uint32_t value) {
    return (uint32_t)TOLOWER(value);
}

static const goat_module_services_v1 services = {sizeof(goat_module_services_v1),
                                                 0,
                                                 allocate_memory,
                                                 deallocate_memory,
                                                 deallocate_all,
                                                 is_space,
                                                 to_upper,
                                                 to_lower};

static bool contains(const void *base, size_t size, const void *pointer, size_t length) {
    uintptr_t begin = (uintptr_t)base, address = (uintptr_t)pointer;
    return address >= begin && address - begin <= size && length <= size - (address - begin);
}

static bool readable(void *state, const void *pointer, size_t length) {
    call_memory_t *memory = state;
    for (call_block_t *block = memory->blocks; block; block = block->next)
        if (contains(block + 1, block->size, pointer, length))
            return true;
    if (contains(memory->strings, memory->count * sizeof(*memory->strings), pointer, length))
        return true;
    for (size_t i = 0; i < memory->count; i++)
        if (contains(memory->strings[i].data,
                     (size_t)memory->strings[i].length * sizeof(uint32_t),
                     pointer,
                     length))
            return true;
    return memory->entry.readable
           && memory->entry.readable(memory->entry.readable_state, pointer, length);
}

static uint32_t integer_field(const yyjson_val *value, const char *name) {
    int64_t number = 0;
    if (!goat_json_integer(yyjson_obj_get(value, name), &number) || number < 1
        || (uint64_t)number > UINT32_MAX)
        return 0;
    return (uint32_t)number;
}

bool bind_module_files_native(module_files_t *files,
                              const yyjson_val *variant,
                              bool required,
                              module_native_resolver_t resolver,
                              void *state,
                              void (*cleanup)(void *)) {
    if (!files || !variant || !resolver || module_files_native(files)
        || goat_module_abi_validate(variant) != GOAT_ABI_OK)
        return false;
    bytecode_t *code = module_files_bytecode(files);
    yyjson_val *specs = yyjson_obj_get(variant, "specializations");
    size_t i, max;
    yyjson_val *spec;
    yyjson_arr_foreach(specs, i, max, spec) {
        uint32_t id = integer_field(spec, "functionId");
        const bytecode_function_t *function = NULL;
        for (size_t j = 0; j < code->function_count; j++)
            if (code->functions[j].id == id)
                function = code->functions + j;
        yyjson_val *signature = yyjson_obj_get(spec, "signature");
        if (!function
            || yyjson_arr_size(yyjson_obj_get(signature, "arguments")) != function->parameter_count)
            return false;
    }
    size_t size = 0;
    char *json = yyjson_val_write(variant, 0, &size);
    if (!json)
        return false;
    yyjson_doc *copy = yyjson_read(json, size, 0);
    free(json);
    if (!copy)
        return false;
    module_native_binding_t *binding = CALLOC(sizeof(*binding));
    binding->metadata = copy;
    binding->required = required;
    binding->resolve = resolver;
    binding->state = state;
    binding->cleanup = cleanup;
    if (!set_module_files_native(files, binding)) {
        yyjson_doc_free(copy);
        FREE(binding);
        return false;
    }
    return true;
}

void destroy_module_native_binding(module_native_binding_t *binding) {
    if (binding) {
        if (binding->cleanup)
            binding->cleanup(binding->state);
        yyjson_doc_free(binding->metadata);
        FREE(binding);
    }
}

static void
diagnostic(module_files_t *files, uint32_t function, const char *requirement, thread_t *thread) {
    char *name = encode_utf8_view(module_files_name(files), NULL);
    char *version = encode_utf8_view(module_files_version(files), NULL);
    size_t size = allocation_size_add(strlen(name), strlen(version));
    size = allocation_size_add(size, strlen(requirement) + 100);
    FREE(thread->native_diagnostic);
    thread->native_diagnostic = ALLOC(size);
    snprintf(thread->native_diagnostic,
             size,
             "Module %s@%s, function %u: %s",
             name,
             version,
             function,
             requirement);
    FREE(name);
    FREE(version);
}

static module_native_call_status_t failure(module_files_t *files,
                                           const bytecode_function_t *function,
                                           const char *requirement,
                                           uint32_t status,
                                           thread_t *thread) {
    thread->process->native_failed = true;
    broadcast_os_condition(thread->process->threads_changed);
    thread->native_status = status ? status : GOAT_MODULE_BAD_REQUEST;
    diagnostic(files, function->id, requirement, thread);
    clear_exception(&thread->exception);
    thread->exception.trace = capture_native_exception_trace(thread->context,
                                                             module_files_identity(files),
                                                             function->file,
                                                             function->body);
    return MODULE_NATIVE_ERROR;
}

static bool
convert_argument(object_t *object, goat_module_value_v1 *value, goat_module_string_v1 *string) {
    if (object == get_null_object()) {
        value->tag = GOAT_MODULE_NULL;
    } else if (is_integer_object(object)) {
        value->tag = GOAT_MODULE_INTEGER;
        int64_t number = get_object_integer_value(object).value;
        memcpy(&value->payload, &number, sizeof(number));
    } else if (is_real_object(object)) {
        value->tag = GOAT_MODULE_REAL;
        double number = get_object_real_value(object).value;
        memcpy(&value->payload, &number, sizeof(number));
    } else if (is_character_object(object)) {
        value->tag = GOAT_MODULE_CHARACTER;
        value->payload = (uint32_t)get_object_integer_value(object).value;
    } else if (object->vtbl->type == TYPE_BOOLEAN) {
        value->tag = GOAT_MODULE_BOOLEAN;
        value->payload = get_object_boolean_value(object) ? 1 : 0;
    } else if (object->vtbl->type == TYPE_STRING) {
        string_value_t text = convert_object_to_string(object);
        string->data = (const uint32_t *)text.data;
        string->length = text.length;
        value->tag = GOAT_MODULE_STRING;
        value->payload = (uintptr_t)string;
    } else {
        return false;
    }
    return true;
}

static object_t *convert_result(const goat_module_value_v1 *value, process_t *process) {
    switch (value->tag) {
        case GOAT_MODULE_NULL:
            return get_null_object();
        case GOAT_MODULE_BOOLEAN:
            return get_boolean_object(value->payload != 0);
        case GOAT_MODULE_CHARACTER:
            return create_character_object(process, (gchar_t)value->payload);
        case GOAT_MODULE_INTEGER: {
            int64_t number;
            memcpy(&number, &value->payload, sizeof(number));
            return create_integer_object(process, number);
        }
        case GOAT_MODULE_REAL: {
            double number;
            memcpy(&number, &value->payload, sizeof(number));
            return create_real_number_object(process, number);
        }
        default: {
            const goat_module_string_v1 *string = (const void *)(uintptr_t)value->payload;
            return create_string_object(
                process,
                (string_value_t){(const gchar_t *)string->data, (size_t)string->length, false});
        }
    }
}

module_native_call_status_t call_module_native(module_files_t *files,
                                               uint32_t file_id,
                                               uint32_t function_id,
                                               instr_index_t body,
                                               size_t formal_count,
                                               uint16_t actual_count,
                                               thread_t *thread) {
    if (thread->process->native_failed)
        return MODULE_NATIVE_ERROR;
    module_native_binding_t *binding = module_files_native(files);
    if (!binding || binding->unavailable)
        return MODULE_NATIVE_FALLBACK;
    const bytecode_t *code = module_files_bytecode(files);
    const bytecode_function_t *function = NULL;
    for (size_t i = 0; i < code->function_count; i++)
        if (code->functions[i].id == function_id && code->functions[i].body == body
            && code->functions[i].file == file_id)
            function = code->functions + i;
    if (!function || function->parameter_count != formal_count)
        return MODULE_NATIVE_FALLBACK;
    goat_module_value_v1 *arguments =
        CALLOC(allocation_size_multiply(formal_count, sizeof(*arguments)));
    goat_module_string_v1 *strings =
        CALLOC(allocation_size_multiply(formal_count, sizeof(*strings)));
    call_memory_t memory = {.strings = strings, .count = formal_count};
    module_native_call_status_t outcome = MODULE_NATIVE_FALLBACK;
    for (size_t i = 0; i < formal_count; i++) {
        object_t *arg = i < actual_count
                            ? peek_object_from_stack(thread->data_stack, actual_count - 1 - i)
                            : get_null_object();
        if (!convert_argument(arg, arguments + i, strings + i)
            || !goat_module_value_valid(arguments + i, readable, &memory))
            goto done;
    }
    yyjson_val *specs = yyjson_obj_get(yyjson_doc_get_root(binding->metadata), "specializations");
    yyjson_val *selected = NULL, *spec;
    size_t i, max;
    yyjson_arr_foreach(specs, i, max, spec) {
        if (integer_field(spec, "functionId") == function->id
            && goat_module_guard_matches(spec, formal_count, arguments)) {
            if (selected) {
                outcome = failure(files,
                                  function,
                                  "ambiguous native specialization guards",
                                  GOAT_MODULE_BAD_REQUEST,
                                  thread);
                goto done;
            }
            selected = spec;
        }
    }
    if (!selected || !native_stack_has_headroom())
        goto done;
    bool resolved = binding->resolve(binding->state,
                                     integer_field(selected, "specializationId"),
                                     yyjson_get_str(yyjson_obj_get(selected, "symbol")),
                                     &memory.entry);
    /* Loading can release the execution lock. A different thread may terminate
       the run during I/O; do not begin native code or replace its exit status. */
    if (thread->process->native_failed || thread->process->exit_requested) {
        outcome = MODULE_NATIVE_ERROR;
        goto done;
    }
    if (!resolved || !memory.entry.adapter) {
        if (binding->required || memory.entry.integrity_failed) {
            outcome = failure(files,
                              function,
                              memory.entry.failure_reason
                                  ? memory.entry.failure_reason
                                  : "selected native library or ABI could not be resolved",
                              GOAT_MODULE_EXTERNAL_ERROR,
                              thread);
        } else {
            binding->unavailable = true;
            if (!memory.entry.failure_reported) {
                diagnostic(files, function->id, "native library unavailable; using VM", thread);
                FPRINTF(stderr, "%s\n", thread->native_diagnostic);
                FREE(thread->native_diagnostic);
                thread->native_diagnostic = NULL;
            }
        }
        goto done;
    }
    goat_module_context_v1 context = {&services, &memory};
    goat_module_value_v1 result;
    uint32_t native_status;
    uint32_t result_tag =
        goat_module_scalar_tag(yyjson_obj_get(yyjson_obj_get(selected, "signature"), "result"));
    thread->native_attempts++;
    size_t depth = process_execution_suspend(thread->process);
    goat_module_abi_error error = goat_module_abi_invoke(memory.entry.adapter,
                                                         result_tag,
                                                         &context,
                                                         (uint32_t)formal_count,
                                                         arguments,
                                                         &result,
                                                         readable,
                                                         &memory,
                                                         &native_status);
    process_execution_resume(thread->process, depth);
    if (error != GOAT_ABI_OK) {
        outcome = failure(files,
                          function,
                          error == GOAT_ABI_VALUE
                              ? "native result violates declared ABI type or value representation"
                              : "native adapter failed after entry; VM replay is forbidden",
                          native_status == UINT32_MAX ? GOAT_MODULE_BAD_REQUEST : native_status,
                          thread);
        goto done;
    }
    object_t *value = convert_result(&result, thread->process);
    for (uint16_t j = 0; j < actual_count; j++) {
        object_t *arg = pop_object_from_stack(thread->data_stack);
        DECREF(arg);
    }
    push_object_onto_stack(thread->data_stack, value);
    thread->native_successes++;
    thread->instr_id++;
    outcome = MODULE_NATIVE_COMPLETE;
done:
    deallocate_all(&memory);
    FREE(strings);
    FREE(arguments);
    return outcome;
}
