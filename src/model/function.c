/**
 * @file function.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementations of an object representing a function.
 */

#include "analysis/builtin_function.h"
#include "builtin_function.h"
#include "common_methods.h"
#include "context.h"
#include "lib/allocate.h"
#include "lib/io.h"
#include "object.h"
#include "object_state.h"
#include "process.h"
#include "thread.h"

#include <math.h>

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

/** @brief Runtime singleton backed by a shared native descriptor. */
typedef struct {
    object_t base;
    const builtin_function_t *descriptor;
} object_static_function_t;

/** @brief Implements @ref object_vtbl_t::to_string. */
static string_value_t static_to_string(const object_t *obj) {
    object_static_function_t *sfobj = (object_static_function_t *)obj;
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

/** @brief Invokes a native descriptor; failed calls leave an owned pending exception. */
static bool static_call(object_t *obj, uint16_t arg_count, thread_t *thread) {
    require_object_stack_size(thread->data_stack, arg_count);
    const builtin_function_t *descriptor = ((object_static_function_t *)obj)->descriptor;
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

/** @brief Macro for defining the start and end of a built-in function. */
#define START_FUNCTION(func_name)                                                                  \
    static operation_result_t func_name##_exec(object_t **args,                                    \
                                               uint16_t arg_count,                                 \
                                               thread_t *thread) {
#define END_FUNCTION(func_name, func_label, arity, effect_flags, abstract_exec)                    \
    }                                                                                              \
    static const builtin_function_t func_name##_descriptor = {.name = func_label,                  \
                                                              .min_args = arity,                   \
                                                              .effects = effect_flags,             \
                                                              .execute = func_name##_exec,         \
                                                              .interpret = abstract_exec,          \
                                                              .get_object = get_##func_name};      \
    static object_static_function_t func_name = {{&static_vtbl, NULL, NULL, NULL},                 \
                                                 &func_name##_descriptor};                         \
    object_t *get_##func_name() {                                                                  \
        return &func_name.base;                                                                    \
    }

/** @brief Built-in atan2(y, x); requires two numeric arguments. */
START_FUNCTION(function_atan)
real_value_t y = get_object_real_value(args[0]);
real_value_t x = get_object_real_value(args[1]);
if (!x.has_value || !y.has_value) {
    return operation_exception(get_exception_invalid_argument());
}
double result = atan2(y.value, x.value);
return operation_success(create_real_number_object(thread->process, result));
END_FUNCTION(function_atan, L"atan", 2, BUILTIN_EFFECT_NONE, interpret_builtin_atan);

/** @brief Built-in print; writes the first argument and returns the null object. */
START_FUNCTION(function_print)
string_value_t str = convert_object_to_string(args[0]);
if (str.data) {
    print_utf8(str.data);
    FREE_STRING(str);
}
return operation_success(get_null_object());
END_FUNCTION(function_print, L"print", 1, BUILTIN_EFFECT_OUTPUT, interpret_builtin_print);

/** @brief Built-in function: determines the sign of a given number. */
START_FUNCTION(function_sign)
real_value_t argument = get_object_real_value(args[0]);
if (!argument.has_value)
    return operation_exception(get_exception_invalid_argument());
double value = argument.value;
int sign;
if (value > 0) {
    sign = 1;
} else if (value < 0) {
    sign = -1;
} else {
    sign = 0;
}
return operation_success(get_static_integer_object(sign));
END_FUNCTION(function_sign, L"sign", 1, BUILTIN_EFFECT_NONE, interpret_builtin_sign);

/**
 * @brief Built-in function: computes the square root of a number.
 * @return A real result, or INVALID_ARGUMENT for a nonnumeric argument.
 */
START_FUNCTION(function_sqrt)
real_value_t argument = get_object_real_value(args[0]);
if (!argument.has_value)
    return operation_exception(get_exception_invalid_argument());
double value = argument.value;
double result = sqrt(value);
return operation_success(create_real_number_object(thread->process, result));
END_FUNCTION(function_sqrt, L"sqrt", 1, BUILTIN_EFFECT_NONE, interpret_builtin_sqrt);

const builtin_function_t *const *get_builtin_functions(size_t *count) {
    static const builtin_function_t *const functions[] = {&function_atan_descriptor,
                                                          &function_print_descriptor,
                                                          &function_sign_descriptor,
                                                          &function_sqrt_descriptor};
    *count = sizeof(functions) / sizeof(*functions);
    return functions;
}

const builtin_function_t *find_builtin_function(string_view_t name) {
    size_t count;
    const builtin_function_t *const *functions = get_builtin_functions(&count);
    for (size_t i = 0; i < count; i++) {
        if (wcslen(functions[i]->name) == name.length
            && !wmemcmp(functions[i]->name, name.data, name.length))
            return functions[i];
    }
    return NULL;
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
    dfobj->state = MARKED;
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

/**
 * @brief Executes a dynamic function object.
 * @return `true` indicating the call was successful.
 */
static bool dynamic_call(object_t *obj, uint16_t arg_count, thread_t *thread) {
    require_object_stack_size(thread->data_stack, arg_count);
    object_dynamic_function_t *dfobj = (object_dynamic_function_t *)obj;
    context_t *ctx = create_context(thread->process, thread->context, dfobj->closure);
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
                                 object_t *closure) {
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
    obj->closure = closure;
    INCREF(closure);
    add_object_to_list(&process->objects, &obj->base);
    return &obj->base;
}
