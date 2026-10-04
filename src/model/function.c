/**
 * @file function.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementations of an object representing a function.
 */

#include "builtin_function.h"
#include "common_methods.h"
#include "context.h"
#include "lib/allocate.h"
#include "native_library.h"
#include "object.h"
#include "object_state.h"
#include "process.h"
#include "thread.h"

/** @brief Implements @ref object_vtbl_t::get_keys. */
static object_array_t get_keys(const object_t *obj) {
    return (object_array_t){NULL, 0};
}

/** @brief Implements @ref object_vtbl_t::get_property. */
static object_t *get_property(const object_t *obj, const object_t *key) {
    return NULL;
}

/** @brief Virtual table defining the behavior of the prototype function object. */
static object_vtbl_t function_proto_vtbl = {.type = TYPE_OTHER,
                                            .inc_ref = stub_memory_function,
                                            .dec_ref = stub_memory_function,
                                            .mark = stub_memory_function,
                                            .sweep = no_sweep,
                                            .release = stub_memory_function,
                                            .compare = compare_object_addresses,
                                            .clone = clone_singleton,
                                            .to_string = common_to_string,
                                            .to_string_notation = common_to_string_notation,
                                            .get_prototypes = common_get_prototypes,
                                            .get_topology = common_get_topology,
                                            .get_keys = get_keys,
                                            .get_property = get_property,
                                            .create_property = create_property_on_immutable,
                                            .set_property = set_property_on_immutable,
                                            .bitwise_not = stub_unary_operation,
                                            .bitwise_and = stub_bitwise,
                                            .bitwise_or = stub_bitwise,
                                            .bitwise_xor = stub_bitwise,
                                            .shift_left = stub_bitwise,
                                            .shift_right = stub_bitwise,
                                            .unary_plus = stub_unary_operation,
                                            .unary_minus = stub_unary_operation,
                                            .increment = stub_unary_operation,
                                            .decrement = stub_unary_operation,
                                            .add = stub_add,
                                            .subtract = stub_subtract,
                                            .multiply = stub_multiply,
                                            .divide = stub_divide,
                                            .modulo = stub_modulo,
                                            .power = stub_power,
                                            .less = common_less,
                                            .less_or_equal = common_less_or_equal,
                                            .greater = common_greater,
                                            .greater_or_equal = common_greater_or_equal,
                                            .equal = common_equal,
                                            .not_equal = common_not_equal,
                                            .get_boolean_value = stub_get_boolean_value,
                                            .get_integer_value = stub_get_integer_value,
                                            .get_real_value = stub_get_real_value,
                                            .call = stub_call};

/** @brief The prototype function object. */
static object_t function_proto = {.vtbl = &function_proto_vtbl};

object_t *get_function_proto() {
    return &function_proto;
}

/** @brief Implements @ref object_vtbl_t::to_string. */
static string_value_t static_to_string(const object_t *obj) {
    builtin_function_object_t *sfobj = (builtin_function_object_t *)obj;
    return (string_value_t){sfobj->descriptor->name, wcslen(sfobj->descriptor->name), false};
}

/** @brief Implements @ref object_vtbl_t::to_string_notation. */
static string_value_t static_to_string_notation(const object_t *obj) {
    return static_to_string(obj);
}

/** @brief Array of prototypes for the function object. */
static object_t *prototypes[] = {&function_proto};

/** @brief Implements @ref object_vtbl_t::get_prototypes. */
static object_array_t get_prototypes(const object_t *obj) {
    object_array_t result = {.items = prototypes, .size = 1};
    return result;
}

/** @brief Implements @ref object_vtbl_t::get_topology. */
static object_array_t get_topology(const object_t *obj) {
    static object_t *topology[2] = {0};
    if (topology[0] == NULL) {
        topology[0] = &function_proto;
        topology[1] = get_root_object();
    }
    object_array_t result = {.items = topology, .size = 2};
    return result;
}

/** @brief Invokes a builtin; failed calls leave an owned pending exception. */
static bool static_call(object_t *obj, uint16_t arg_count, thread_t *thread) {
    require_object_stack_size(thread->data_stack, arg_count);
    const builtin_function_t *descriptor = ((builtin_function_object_t *)obj)->descriptor;
    object_t **args = arg_count ? ALLOC(arg_count * sizeof(*args)) : NULL;
    for (size_t i = 0; i < arg_count; i++)
        args[i] = pop_object_from_stack(thread->data_stack);
    operation_result_t result = arg_count < descriptor->min_args
                                    ? operation_exception(get_exception_invalid_argument())
                                    : descriptor->execute(args, arg_count, thread);
    for (size_t i = 0; i < arg_count; i++)
        DECREF(args[i]);
    FREE(args);
    if (result.is_exception) {
        DECREFIF(thread->exception.value);
        thread->exception.value = result.value;
        return false;
    }
    push_object_onto_stack(thread->data_stack, result.value);
    thread->instr_id++;
    return true;
}

/** @brief Virtual table defining the behavior of the static functional object. */
static object_vtbl_t static_vtbl = {.type = TYPE_OTHER,
                                    .inc_ref = stub_memory_function,
                                    .dec_ref = stub_memory_function,
                                    .mark = stub_memory_function,
                                    .sweep = no_sweep,
                                    .release = stub_memory_function,
                                    .compare = compare_object_addresses,
                                    .clone = clone_singleton,
                                    .to_string = static_to_string,
                                    .to_string_notation = static_to_string_notation,
                                    .get_prototypes = get_prototypes,
                                    .get_topology = get_topology,
                                    .get_keys = get_keys,
                                    .get_property = get_property,
                                    .create_property = create_property_on_immutable,
                                    .set_property = set_property_on_immutable,
                                    .bitwise_not = stub_unary_operation,
                                    .bitwise_and = stub_bitwise,
                                    .bitwise_or = stub_bitwise,
                                    .bitwise_xor = stub_bitwise,
                                    .shift_left = stub_bitwise,
                                    .shift_right = stub_bitwise,
                                    .unary_plus = stub_unary_operation,
                                    .unary_minus = stub_unary_operation,
                                    .increment = stub_unary_operation,
                                    .decrement = stub_unary_operation,
                                    .add = stub_add,
                                    .subtract = stub_subtract,
                                    .multiply = stub_multiply,
                                    .divide = stub_divide,
                                    .modulo = stub_modulo,
                                    .power = stub_power,
                                    .less = common_less,
                                    .less_or_equal = common_less_or_equal,
                                    .greater = common_greater,
                                    .greater_or_equal = common_greater_or_equal,
                                    .equal = common_equal,
                                    .not_equal = common_not_equal,
                                    .get_boolean_value = stub_get_boolean_value,
                                    .get_integer_value = stub_get_integer_value,
                                    .get_real_value = stub_get_real_value,
                                    .call = static_call};

object_t *get_builtin_function_object(builtin_function_object_t *storage,
                                      const builtin_function_t *descriptor) {
    if (!storage->base.vtbl) {
        storage->base.vtbl = &static_vtbl;
        storage->descriptor = descriptor;
    }
    return &storage->base;
}

/** @brief Function body address, argument names, and captured context. */
typedef struct {
    /** @brief The base object that provides common functionality. */
    object_t base;

    /** @brief Reference count used for garbage collection. */
    int refs;

    /** @brief The state of the object (unmarked, marked, zombie, or dying). */
    object_state_t state;

    /** @brief Array of argument name objects. */
    object_t **arg_names;

    /** @brief The number of arguments the function accepts. */
    size_t arg_count;

    /** @brief The starting instruction ID for this function. */
    instr_index_t first_instr_id;

    /** @brief The lexical closure environment of the function. */
    object_t *closure;

    /** @brief Keeps specialization metadata and library code alive. */
    native_function_descriptor_t *native_function;
} object_dynamic_function_t;

/** @brief Releases or clears a dynamic function object. */
static void clear(object_dynamic_function_t *dfobj, bool deep_cleaning) {
    remove_object_from_list(&dfobj->base.process->objects, &dfobj->base);
    if (deep_cleaning) {
        for (size_t index = 0; index < dfobj->arg_count; index++) {
            DECREF(dfobj->arg_names[index]);
        }
        DECREF(dfobj->closure);
    }
    release_native_function_descriptor(dfobj->native_function);
    FREE(dfobj->arg_names);
    FREE(dfobj);
}

/** @brief Implements @ref object_vtbl_t::inc_ref. */
static void inc_ref(object_t *obj) {
    object_dynamic_function_t *dfobj = (object_dynamic_function_t *)obj;
    dfobj->refs++;
}

/** @brief Implements @ref object_vtbl_t::dec_ref. */
static void dec_ref(object_t *obj) {
    object_dynamic_function_t *dfobj = (object_dynamic_function_t *)obj;
    if (!(--dfobj->refs)) {
        clear(dfobj, true);
    }
}

/** @brief Implements @ref object_vtbl_t::mark. */
static void mark(object_t *obj) {
    object_dynamic_function_t *dfobj = (object_dynamic_function_t *)obj;
    if (dfobj->state == MARKED)
        return;
    dfobj->state = MARKED;
    mark_object(dfobj->closure);
    for (size_t i = 0; i < dfobj->arg_count; i++)
        mark_object(dfobj->arg_names[i]);
}

/** @brief Implements @ref object_vtbl_t::sweep. */
static bool sweep(object_t *obj) {
    object_dynamic_function_t *dfobj = (object_dynamic_function_t *)obj;
    if (dfobj->state == UNMARKED) {
        clear(dfobj, false);
        return true;
    } else {
        dfobj->state = UNMARKED;
        return false;
    }
}

/** @brief Implements @ref object_vtbl_t::release. */
static void release(object_t *obj) {
    object_dynamic_function_t *dfobj = (object_dynamic_function_t *)obj;
    clear(dfobj, false);
}

/** @brief Implements @ref object_vtbl_t::to_string. */
static string_value_t dynamic_to_string(const object_t *obj) {
    return STATIC_STRING(L"func");
}

/** @brief Implements @ref object_vtbl_t::to_string_notation. */
static string_value_t dynamic_to_string_notation(const object_t *obj) {
    return dynamic_to_string(obj);
}

/** @brief Selects an exact formal-parameter signature without consuming arguments. */
static const goat_native_entry_v1_t *select_native_entry(const object_dynamic_function_t *function,
                                                         uint16_t count,
                                                         object_stack_t *stack) {
    if (count < function->arg_count)
        return NULL;
    for (uint32_t i = 0; i < get_native_function_entry_count(function->native_function); i++) {
        const goat_native_entry_v1_t *entry =
            get_native_function_entry(function->native_function, i);
        if (entry->parameter_count != function->arg_count)
            continue;
        size_t j = 0;
        for (; j < function->arg_count; j++) {
            object_t *arg = peek_object_from_stack(stack, j);
            uint32_t type = is_integer_object(arg) ? GOAT_NATIVE_I64
                            : is_real_object(arg)  ? GOAT_NATIVE_F64
                                                   : GOAT_NATIVE_INVALID;
            if (entry->parameter_types[j] != type)
                break;
        }
        if (j == function->arg_count)
            return entry;
    }
    return NULL;
}

/** @brief Commits one numeric result; backend failures never execute the body a second time. */
static bool
invoke_native_entry(const goat_native_entry_v1_t *entry, uint16_t count, thread_t *thread) {
    goat_native_value_v1_t *args =
        entry->parameter_count ? CALLOC(entry->parameter_count * sizeof(*args)) : NULL;
    for (uint32_t i = 0; i < entry->parameter_count; i++) {
        object_t *arg = peek_object_from_stack(thread->data_stack, i);
        args[i].type = entry->parameter_types[i];
        if (args[i].type == GOAT_NATIVE_I64)
            args[i].value.integer = get_object_integer_value(arg).value;
        else
            args[i].value.real = get_object_real_value(arg).value;
    }
    goat_native_value_v1_t result = {0};
    thread->native_attempts++;
    uint32_t status = entry->invoke(GOAT_NATIVE_ABI_VERSION, entry->parameter_count, args, &result);
    FREE(args);
    if (status == GOAT_NATIVE_OK && (result.type != entry->return_type || result.reserved))
        status = GOAT_NATIVE_BAD_REQUEST;
    thread->native_status = status;
    if (status != GOAT_NATIVE_OK)
        return false;
    thread->native_successes++;
    object_t *value = result.type == GOAT_NATIVE_I64
                          ? create_integer_object(thread->process, result.value.integer)
                          : create_real_number_object(thread->process, result.value.real);
    for (uint16_t i = 0; i < count; i++) {
        object_t *arg = pop_object_from_stack(thread->data_stack);
        DECREF(arg);
    }
    push_object_onto_stack(thread->data_stack, value);
    thread->instr_id++;
    return true;
}

/** @brief Chooses native execution before allocating a bytecode call context. */
static bool dynamic_call(object_t *obj, uint16_t arg_count, thread_t *thread) {
    require_object_stack_size(thread->data_stack, arg_count);
    object_dynamic_function_t *dfobj = (object_dynamic_function_t *)obj;
    const goat_native_entry_v1_t *entry =
        thread->context->native_disabled
            ? NULL
            : select_native_entry(dfobj, arg_count, thread->data_stack);
    bool retry = false;
    if (entry) {
        if (!native_stack_has_headroom()) {
            retry = true;
        } else if (invoke_native_entry(entry, arg_count, thread)) {
            return true;
        } else if (thread->native_status == GOAT_NATIVE_RESOURCE_LIMIT
                   && (entry->flags & GOAT_NATIVE_PURE)) {
            retry = true;
        } else {
            return false;
        }
    }
    if (retry) {
        thread->native_retries++;
        thread->native_status = GOAT_NATIVE_OK;
    }
    context_t *ctx = create_context(thread->process, thread->context, dfobj->closure);
    ctx->native_disabled |= retry;
    ctx->control_flow = FLOW_RETURN;
    ctx->jump_address[0] = thread->instr_id + 1;
    uint16_t index;
    for (index = 0; index < arg_count && index < dfobj->arg_count; index++) {
        object_t *arg = pop_object_from_stack(thread->data_stack);
        create_object_property(ctx->data, dfobj->arg_names[index], arg, false);
        DECREF(arg);
    }
    for (; index < dfobj->arg_count; index++) {
        create_object_property(ctx->data, dfobj->arg_names[index], get_null_object(), false);
    }
    /* Extra actual arguments have been evaluated but have no parameter binding. */
    for (size_t extra = dfobj->arg_count; extra < arg_count; extra++) {
        object_t *arg = pop_object_from_stack(thread->data_stack);
        DECREF(arg);
    }
    stack_index_t ret_value_index = push_object_onto_stack(thread->data_stack, get_null_object());
    ctx->ret_value_index = ret_value_index;
    ctx->unwinding_index = ret_value_index;
    thread->context = ctx;
    thread->instr_id = dfobj->first_instr_id;
    return true;
}

/** @brief Virtual table defining the behavior of the dynamic functional object. */
static object_vtbl_t dynamic_vtbl = {.type = TYPE_OTHER,
                                     .inc_ref = inc_ref,
                                     .dec_ref = dec_ref,
                                     .mark = mark,
                                     .sweep = sweep,
                                     .release = release,
                                     .compare = compare_object_addresses,
                                     .clone = clone_singleton,
                                     .to_string = dynamic_to_string,
                                     .to_string_notation = dynamic_to_string_notation,
                                     .get_prototypes = get_prototypes,
                                     .get_topology = get_topology,
                                     .get_keys = get_keys,
                                     .get_property = get_property,
                                     .create_property = create_property_on_immutable,
                                     .set_property = set_property_on_immutable,
                                     .bitwise_not = stub_unary_operation,
                                     .bitwise_and = stub_bitwise,
                                     .bitwise_or = stub_bitwise,
                                     .bitwise_xor = stub_bitwise,
                                     .shift_left = stub_bitwise,
                                     .shift_right = stub_bitwise,
                                     .unary_plus = stub_unary_operation,
                                     .unary_minus = stub_unary_operation,
                                     .increment = stub_unary_operation,
                                     .decrement = stub_unary_operation,
                                     .add = stub_add,
                                     .subtract = stub_subtract,
                                     .multiply = stub_multiply,
                                     .divide = stub_divide,
                                     .modulo = stub_modulo,
                                     .power = stub_power,
                                     .less = common_less,
                                     .less_or_equal = common_less_or_equal,
                                     .greater = common_greater,
                                     .greater_or_equal = common_greater_or_equal,
                                     .equal = common_equal,
                                     .not_equal = common_not_equal,
                                     .get_boolean_value = stub_get_boolean_value,
                                     .get_integer_value = stub_get_integer_value,
                                     .get_real_value = stub_get_real_value,
                                     .call = dynamic_call};

object_t *create_function_object(process_t *process,
                                 object_t **arg_names,
                                 size_t arg_count,
                                 instr_index_t first_instr_id,
                                 object_t *closure,
                                 native_function_descriptor_t *native_function) {
    object_dynamic_function_t *obj =
        (object_dynamic_function_t *)CALLOC(sizeof(object_dynamic_function_t));
    obj->base.vtbl = &dynamic_vtbl;
    obj->base.process = process;
    obj->refs = 1;
    obj->state = UNMARKED;
    obj->arg_names = arg_names;
    obj->arg_count = arg_count;
    for (size_t index = 0; index < arg_count; index++) {
        INCREF(arg_names[index]);
    }
    obj->first_instr_id = first_instr_id;
    obj->native_function = retain_native_function_descriptor(native_function);
    obj->closure = closure;
    INCREF(closure);
    add_object_to_list(&process->objects, &obj->base);
    return &obj->base;
}
