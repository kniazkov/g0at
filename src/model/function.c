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

#include "object.h"
#include "thread.h"
#include "common_methods.h"
#include "lib/allocate.h"

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
    .sweep = stub_memory_function,
    .release = stub_memory_function,
    .compare = compare_object_addresses,
    .clone = clone_singleton,
    .to_string = common_to_string,
    .to_string_notation = common_to_string_notation,
    .get_prototypes = common_get_prototypes,
    .get_topology = common_get_topology,
    .get_keys = get_keys,
    .get_property = get_property,
    .set_property = set_property_on_immutable,
    .add = stub_add,
    .sub = stub_sub,
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
        for (uint16_t i = 0; i < arg_count; i++) {
            args[i] = pop_object_from_stack(thread->data_stack);
        }
        ret_val = sfobj->exec(args, arg_count, thread);
        FREE(args);
    }
    push_object_onto_stack(thread->data_stack, ret_val);
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
    .sweep = stub_memory_function,
    .release = stub_memory_function,
    .compare = compare_object_addresses,
    .clone = clone_singleton,
    .to_string = static_to_string,
    .to_string_notation = static_to_string_notation,
    .get_prototypes = get_prototypes,
    .get_topology = get_topology,
    .get_keys = get_keys,
    .get_property = get_property,
    .set_property = set_property_on_immutable,
    .add = stub_add,
    .sub = stub_sub,
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
