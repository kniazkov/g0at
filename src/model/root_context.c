/**
 * @file root_context.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines the root context for the Goat programming language execution.
 * 
 * The root context serves as the initial execution context when the Goat program starts. 
 * It contains all the built-in objects and functions that are available from the moment
 * the program begins. This includes essential objects such as primitive types, constants, 
 * and functions, as well as the prototype objects that form the basis for object-oriented behavior
 * in Goat.
 * 
 * As a singleton, the root context is unique for the entire execution of the program. 
 * It is used to initialize the execution environment and is typically referenced by the program
 * during the initial setup phase, where the context's data forms the foundation for all subsequent
 * program execution.
 */

#include "context.h"
#include "object.h"
#include "common_methods.h"
#include "lib/pair.h"
#include "lib/string_ext.h"

/**
 * @brief Retrieves all property keys from the root context.
 * 
 * This function returns a static array containing the property keys associated with the root
 * context. The array is initialized lazily during the first invocation of the function.
 * 
 * @param obj The object from which to retrieve the keys (ignored).
 * @return An object array containing all property keys.
 */
static object_array_t get_keys(const object_t *obj) {
    static object_t *keys[2] = { NULL };
    if (keys[0] == NULL) {
        keys[0] = get_string_print();
        keys[1] = get_string_sign();
    }
    return (object_array_t){ keys, sizeof(keys) / sizeof(object_t*) };
}

/**
 * @brief Retrieves the value of a property from the root context.
 * @param obj The root context object (unused in this implementation).
 * @param key The key of the property to retrieve.
 * @return The value of the property if found, otherwise `NULL`.
*/
static object_t *get_property(const object_t *obj, const object_t *key) {
    object_t *value = NULL;
    if (key->vtbl->type == TYPE_STRING) {
        string_value_t key_str = key->vtbl->to_string(key);
        static pair_t properties[] = { 
            { L"pi", get_pi_object },
            { L"print", get_function_print },
            { L"sign", get_function_sign }
            // ... add other properties later
        };
        static_object_getter_t getter = (static_object_getter_t)binary_search(
            properties, sizeof(properties) / sizeof(pair_t), key_str.data, string_comparator
        );
        if (getter) {
            value = getter();
        }
    }
    return value;
}

/**
 * @var vtbl
 * @brief Virtual table defining the behavior of the object containing root context data.
 */
static object_vtbl_t vtbl = {
    .type = TYPE_OTHER,
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
    .get_boolean_value = common_get_boolean_value,
    .get_integer_value = stub_get_integer_value,
    .get_real_value = stub_get_real_value,
    .call = stub_call
};

/**
 * @brief The singleton instance representing the object containing root context data.
 */
static object_t root_context_data = {
    .vtbl = &vtbl
};

/**
 * @brief The singleton instance representing the root context.
 */
static context_t root_context = {
    .data = &root_context_data
};

context_t *get_root_context() {
    return &root_context;
}
