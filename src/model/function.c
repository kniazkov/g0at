/**
 * @file function.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementations of an object representing a function.
 *
 * This file defines the structure and behavior of function objects in the Goat programming
 * language. In Goat, almost everything is treated as an object, and functions are no exception.
 * This approach allows for higher-order functions, where functions can be passed as arguments,
 * returned from other functions, or stored in data structures.
 *
 * There are two types of function objects:
 *
 * 1. Built-in functions (static):
 *    - These functions are predefined as part of the language model.
 *    - They are available immediately upon the virtual machine's startup and persist throughout
 *      the program's execution.
 *    - The scope of these functions is limited to their arguments, and they are not subject
 *      to garbage collection.
 *
 * 2. User-defined functions (dynamic):
 *    - These functions are created by the programmer during runtime.
 *    - A function object is instantiated only when the corresponding instruction is executed.
 *    - The scope of a user-defined function includes its closure, which encapsulates the
 *      function's environment (the variables and context in which it was created). This enables
 *      the function to access variables that were in scope at the time of its creation, even if
 *      they are no longer in scope when the function is executed.
 */

#include <math.h>

#include "object.h"
#include "object_state.h"
#include "context.h"
#include "thread.h"
#include "process.h"
#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/io.h"

/**
 * @brief Retrieves all property keys from an object (stub implementation).
 * 
 * This is a stub implementation of the function to retrieve all keys of the properties 
 * defined on an object. Currently, it returns an empty `object_array_t` as a placeholder.
 * 
 * @param obj The object from which to retrieve the keys.
 * @return An empty `object_array_t` (placeholder implementation).
 */
static object_array_t get_keys(const object_t *obj) {
    return (object_array_t){ NULL, 0 };
}

/**
 * @brief Retrieves the value of a property from an object (stub implementation).
 * 
 * This is a stub implementation of the function to retrieve the value of a property from
 * an object. Currently, it returns `NULL` as a placeholder.
 * 
 * @param obj The object from which to retrieve the property.
 * @param key The key of the property to retrieve.
 * @return Always returns `NULL` (placeholder implementation).
 */
static object_t *get_property(const object_t *obj, const object_t *key) {
    return NULL;
}

/**
 * @var function_proto_vtbl
 * @brief Virtual table defining the behavior of the prototype function object.
 */
static object_vtbl_t function_proto_vtbl = {
    .type = TYPE_STRING,
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
    .add = stub_add,
    .subtract = stub_subtract,
    .get_boolean_value = stub_get_boolean_value,
    .get_integer_value = stub_get_integer_value,
    .get_real_value = stub_get_real_value,
    .call = stub_call
};

/**
 * @var function_proto
 * @brief The prototype function object.
 * 
 * This is the prototype function object, which is the instance that serves as the 
 * prototype for all function objects.
 */
static object_t function_proto = {
    .vtbl = &function_proto_vtbl
};

object_t *get_function_proto() {
    return &function_proto;
}

/**
 * @brief Structure representing a static function object.
 *
 * This structure defines a static function object in the Goat programming language.
 * Static functions are predefined as part of the language model and are always available during
 * the program's execution.
 * 
 * The `exec` function is responsible for executing the logic of the static function.
 * It operates as follows:
 * - Takes an array of arguments, already extracted from the stack (`args`).
 * - Accepts the count of arguments (`arg_count`) as a 16-bit unsigned integer.
 * - Receives a reference to the thread (`thread`) for accessing thread-specific resources or
 *   managing complex operations.
 * 
 * The function returns an `object_t*` representing the result of the execution:
 * - The returned object cannot be `NULL`.
 * - If the function has no return value, a special `null` object is returned.
 */
typedef struct {
    /**
     * @brief The base object that provides common functionality.
     */
    object_t base;

    /**
     * @brief A wide-character string representing the name of the function.
     */
    wchar_t *name;

    /**
     * @brief The wrapped executor function for the static function.
     */
    object_t* (*exec)(object_t** args, uint16_t arg_count, thread_t *thread);
} object_static_function_t;

/**
 * @brief Converts the static function object to a string representation.
 * @param obj The object to convert to a string.
 * @return A `string_value_t` containing the function name.
 */
static string_value_t static_to_string(const object_t *obj) {
    object_static_function_t *sfobj = (object_static_function_t *)obj;
    return (string_value_t){ sfobj->name, wcslen(sfobj->name), false };
}

/**
 * @brief Converts the static function object to a Goat notation string representation.
 * @param obj The object to convert to a Goat notation string.
 * @return A `string_value_t` containing the function name.
 */
static string_value_t static_to_string_notation(const object_t *obj) {
    return static_to_string(obj);
}

/**
 * @var prototypes
 * @brief Array of prototypes for the function object.
 * 
 * It contains only the `function_proto` prototype.
 */
static object_t* prototypes[] = {
    &function_proto
};

/**
 * @brief Retrieves the prototypes of a function object.
 * 
 * This function returns an array of prototypes for a function object.
 * In this case, it contains only the function prototype.
 * 
 * @param obj The object whose prototypes are to be retrieved.
 * @return An object_array_t containing the prototypes of the function object.
 */
static object_array_t get_prototypes(const object_t *obj) {
    object_array_t result = {
        .items = prototypes,
        .size = 1
    };
    return result;
}

/**
 * @brief Retrieves the full prototype topology of a function object.
 * 
 * This function returns the full prototype chain (topology) of a function object.
 * The topology includes the `function_proto` prototype and the root object.
 * 
 * @param obj The object whose prototype topology is to be retrieved.
 * @return An object_array_t containing the full prototype chain.
 */
static object_array_t get_topology(const object_t *obj) {
    static object_t* topology[2] = {0};
    if (topology[0] == NULL) {
        topology[0] = &function_proto;
        topology[1] = get_root_object();
    }
    object_array_t result = {
        .items = topology,
        .size = 2
    };
    return result;
}

/**
 * @brief Executes a static function object.
 *
 * This function is the implementation of the `call` method for static function objects.
 * It retrieves arguments from the thread's data stack, invokes the function using `exec`,
 * and pushes the result back onto the stack.
 *
 * @param obj Pointer to the static function object.
 * @param arg_count The number of arguments passed to the function.
 * @param thread Pointer to the thread in which the function is executed.
 * @return `true` indicating the call was successful.
 */
static bool static_call(object_t *obj, uint16_t arg_count, thread_t *thread) {
    object_static_function_t *sfobj = (object_static_function_t *)obj;
    object_t *ret_val;
    if (arg_count == 0) {
        ret_val = sfobj->exec(NULL, 0, thread);
    } else {
        object_t **args = ALLOC(arg_count * sizeof(object_t*));
        uint16_t index;
        for (index = 0; index < arg_count; index++) {
            args[index] = pop_object_from_stack(thread->data_stack);
        }
        ret_val = sfobj->exec(args, arg_count, thread);
        for (index = 0; index < arg_count; index++) {
            DECREF(args[index]);
        }
        FREE(args);
    }
    push_object_onto_stack(thread->data_stack, ret_val);
    thread->instr_id++;
    return true;
}

/**
 * @var static_vtbl
 * @brief Virtual table defining the behavior of the static functional object.
 */
static object_vtbl_t static_vtbl = {
    .type = TYPE_OTHER,
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
    .add = stub_add,
    .subtract = stub_subtract,
    .get_boolean_value = stub_get_boolean_value,
    .get_integer_value = stub_get_integer_value,
    .get_real_value = stub_get_real_value,
    .call = static_call
};

/**
 * @brief Macro for defining the start and end of a built-in function.
 *
 * This macro simplifies the creation of static function objects. It defines the executor function
 * and the associated static function object, ensuring consistency in naming and initialization.
 *
 * @param func_name The name of the function.
 * @param func_label A wide-character string representing the name of the function.
 */
#define START_FUNCTION(func_name) \
    static object_t *func_name##_exec(object_t** args, uint16_t arg_count, thread_t *thread) {

#define END_FUNCTION(func_name, func_label) \
    } \
    static object_static_function_t func_name = { \
        { &static_vtbl, NULL, NULL, NULL }, \
        func_label, \
        func_name##_exec \
    }; \
    object_t *get_##func_name() { return &func_name.base; }

/**
 * @brief Built-in function: calculates the arc tangent of two values (atan2).
 * 
 * This function computes the angle in radians between the positive X-axis 
 * and the point (x, y) using the standard math function atan2(y, x).
 * 
 * @param args An array of object pointers, where:
 *             - args[0] is the Y coordinate (must be a real number)
 *             - args[1] is the X coordinate (must be a real number)
 * @param arg_count The number of arguments passed to the function.
 * @param thread The current thread executing the function.
 * @return A new real number object containing the result in radians, or:
 *         - NULL if fewer than 2 arguments provided
 *         - NULL if either argument is not a real number
 * @note The result is in the range [-π, π] radians.
 * @note Special cases are handled according to IEEE 754:
 *       - If Y is ±0 and X is negative, returns ±π
 *       - If Y is ±0 and X is positive, returns ±0
 *       - If Y is positive and X is ±0, returns +π/2
 *       - If Y is negative and X is ±0, returns -π/2
 *       - If either argument is NaN, returns NaN
 */
START_FUNCTION(function_atan)
    if (arg_count < 2) {
        return NULL;
    }
    real_value_t y = args[0]->vtbl->get_real_value(args[0]);
    real_value_t x = args[1]->vtbl->get_real_value(args[1]);
    if (!x.has_value || !y.has_value) {
        return NULL;
    }
    double result = atan2(y.value, x.value);
    return create_real_number_object(thread->process, result);
END_FUNCTION(function_atan, L"atan");

/**
 * @brief Built-in function: prints the string representation of an object.
 * 
 * This function converts the first argument to its string representation and then prints it
 * to the standard output.
 * 
 * @param args An array of object pointers, where args[0] is the object to print.
 * @param arg_count The number of arguments passed to the function.
 * @param thread The current thread executing the function.
 * @return A `null` object after the string is printed.
 * 
 * @note The function assumes that the object passed as the first argument has a valid
 *  string representation (i.e., that `to_string` can be called successfully).
 *  If no arguments are provided, the function returns `NULL` without performing any actions.
 */
START_FUNCTION(function_print)
    if (arg_count < 1) {
        return NULL;
    }
    string_value_t str = args[0]->vtbl->to_string(args[0]);
    if (str.data) {
        print_utf8(str.data);
        FREE_STRING(str);
    }
    return get_null_object();
END_FUNCTION(function_print, L"print");

/**
 * @brief Built-in function: determines the sign of a given number.
 * 
 * This function computes the sign of a real number passed as an argument.
 * It returns 1 if the number is positive, -1 if it is negative, and 0 if it is zero.
 * 
 * @param args An array of object pointers, where args[0] is the number to evaluate.
 * @param arg_count The number of arguments passed to the function.
 * @param thread The current thread executing the function.
 * @return A static integer object representing the sign of the input number.
 */
START_FUNCTION(function_sign)
    if (arg_count < 1) {
        return NULL;
    }
    double value = args[0]->vtbl->get_real_value(args[0]).value;
    int sign;
    if (value > 0) {
        sign = 1;
    } else if (value < 0) {
        sign = -1;
    } else {
        sign = 0;
    }
    return get_static_integer_object(sign);
END_FUNCTION(function_sign, L"sign");

/**
 * @brief Structure representing a dynamic function object.
 *
 * This structure defines a dynamically created function in the Goat programming language.
 * Dynamic functions are created during program execution (via the FUNC opcode) and contain:
 * - Their own argument definitions
 * - A reference to their starting instruction
 * - Full garbage collection support
 * - Lexical closure environment
 * 
 * The function's execution is handled by the virtual machine's call mechanism:
 * - Arguments are passed via the data stack
 * - The function creates its own execution context
 * - Return values are pushed to the caller's stack
 */
typedef struct {
    /**
     * @brief The base object that provides common functionality.
     */
    object_t base;

    /**
     * @brief Reference count used for garbage collection.
     */
    int refs;

    /**
     * @brief The state of the object (unmarked, marked, zombie, or dying).
     */
    object_state_t state;

    /**
     * @brief Array of argument name objects.
     */
    object_t **arg_names;

    /**
     * @brief The number of arguments the function accepts.
     */
    size_t arg_count;

    /**
     * @brief The starting instruction ID for this function.
     */
    instr_index_t first_instr_id;

    /**
     * @brief The lexical closure environment of the function.
     * 
     * Contains variables from the function's creation scope that are
     * accessible within the function body. When the function is called,
     * a new context is created using this object as a prototype,
     * allowing the function to access both its arguments and outer scope
     * variables.
     */
    object_t *closure;
} object_dynamic_function_t;

/**
 * @brief Releases or clears a dynamic function object.
 * 
 * This function handles cleanup of dynamic function objects with two modes:
 * 
 * Deep cleaning (used by `dec_ref`):
 * - Decrements references for all argument name objects
 * - Ensures proper cleanup of referenced resources
 * 
 * Shallow cleaning (used by `sweep`):
 * - Only frees immediate object memory
 * - Leaves argument names for garbage collector to handle
 * 
 * Unlike other objects, dynamic functions are always immediately destroyed (no zombie state),
 * don't utilize an object pool (destruction is infrequent).
 * 
 * @param dfobj The dynamic function object to process
 * @param deep_cleaning true to recursively clean argument names,
 *                      false for immediate memory-only cleanup
 */
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

/**
 * @brief Increments the reference count of an object.
 * @param obj The object whose reference count is to be incremented.
 */
static void inc_ref(object_t *obj) {
    object_dynamic_function_t *dfobj = (object_dynamic_function_t *)obj;
    dfobj->refs++;
}

/**
 * @brief Decrements the reference count of an object.
 * @param obj The object whose reference count is to be decremented.
 */
static void dec_ref(object_t *obj) {
    object_dynamic_function_t *dfobj = (object_dynamic_function_t *)obj;
    if (!(--dfobj->refs)) {
        clear(dfobj, true);
    }
}

/**
 * @brief Marks an object as reachable during garbage collection.
 * @param obj The object to mark as reachable.
 */
static void mark(object_t *obj) {
    object_dynamic_function_t *dfobj = (object_dynamic_function_t *)obj;
    dfobj->state = MARKED;
}

/**
 * @brief Sweeps the object (cleaning it up).
 * @param obj The object to sweep.
 * @return true if the object was destroyed,
 *         false if the object was marked (still alive) and shouldn't be processed.
 */
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

/**
 * @brief Releases a dynamic function object.
 * @param obj The object to release.
 */
static void release(object_t *obj) {
    object_dynamic_function_t *dfobj = (object_dynamic_function_t *)obj;
    clear(dfobj, false);
}

/**
 * @brief Converts the dynamic function object to a string representation.
 * @param obj The object to convert to a string.
 * @return A `string_value_t` containing the function name.
 */
static string_value_t dynamic_to_string(const object_t *obj) {
    return STATIC_STRING(L"func");
}

/**
 * @brief Converts the dynamic function object to a Goat notation string representation.
 * @param obj The object to convert to a Goat notation string.
 * @return A `string_value_t` containing the function name.
 */
static string_value_t dynamic_to_string_notation(const object_t *obj) {
    return dynamic_to_string(obj);
}

/**
 * @brief Executes a dynamic function object.
 *
 * This function is the implementation of the `call` method for dynamic function objects...
 *
 * @param obj Pointer to the dynamic function object.
 * @param arg_count The number of arguments passed to the function.
 * @param thread Pointer to the thread in which the function is executed.
 * @return `true` indicating the call was successful.
 */
static bool dynamic_call(object_t *obj, uint16_t arg_count, thread_t *thread) {
    object_dynamic_function_t *dfobj = (object_dynamic_function_t *)obj;
    context_t *ctx = create_context(thread->process, thread->context, dfobj->closure);
    ctx->ret_address = thread->instr_id + 1;
    uint16_t index;
    for (index = 0; index < arg_count && index < dfobj->arg_count; index++) {
        object_t *arg = pop_object_from_stack(thread->data_stack);
        ctx->data->vtbl->create_property(ctx->data, dfobj->arg_names[index], arg, false);
        DECREF(arg);
    }
    for (; index < dfobj->arg_count; index++) {
        ctx->data->vtbl->create_property(ctx->data, dfobj->arg_names[index], get_null_object(), false);
    }
    stack_index_t ret_value_index = push_object_onto_stack(thread->data_stack, get_null_object());
    ctx->ret_value_index = ret_value_index;
    ctx->unwinding_index = ret_value_index;
    thread->context = ctx;
    thread->instr_id = dfobj->first_instr_id;
    return true;
}

/**
 * @var dynamic_vtbl
 * @brief Virtual table defining the behavior of the dynamic functional object.
 */
static object_vtbl_t dynamic_vtbl = {
    .type = TYPE_OTHER,
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
    .add = stub_add,
    .subtract = stub_subtract,
    .get_boolean_value = stub_get_boolean_value,
    .get_integer_value = stub_get_integer_value,
    .get_real_value = stub_get_real_value,
    .call = dynamic_call
};

object_t *create_function_object(process_t *process, object_t **arg_names, size_t arg_count,
        instr_index_t first_instr_id, object_t *closure) {
    object_dynamic_function_t *obj = (object_dynamic_function_t *)ALLOC(
        sizeof(object_dynamic_function_t));
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
