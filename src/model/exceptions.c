/** @file exceptions.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Immutable namespace of ordinary string constants for built-in exceptions.
 */
#include "common_methods.h"
#include "object.h"

/** @brief Implements @ref object_vtbl_t::get_keys. */
static object_array_t get_keys(const object_t *obj) {
    static object_t *keys[7] = {NULL};
    if (!keys[0]) {
        keys[0] = get_exception_division_by_zero();
        keys[1] = get_exception_immutable_object();
        keys[2] = get_exception_invalid_argument();
        keys[3] = get_exception_invalid_operation();
        keys[4] = get_exception_property_already_exists();
        keys[5] = get_exception_property_is_constant();
        keys[6] = get_exception_property_not_found();
    }
    return (object_array_t){keys, sizeof(keys) / sizeof(*keys)};
}

/** @brief Implements @ref object_vtbl_t::get_property; each key is also its value. */
static object_t *get_property(const object_t *obj, const object_t *key) {
    if (key->vtbl->type != TYPE_STRING)
        return NULL;
    object_array_t keys = get_keys(obj);
    for (size_t i = 0; i < keys.size; i++) {
        if (key->vtbl->compare(key, keys.items[i]) == 0)
            return keys.items[i];
    }
    return NULL;
}

/** @brief Virtual table defining the behavior of the Exceptions namespace. */
static object_vtbl_t vtbl = {.type = TYPE_OTHER,
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
                             .get_boolean_value = common_get_boolean_value,
                             .get_integer_value = stub_get_integer_value,
                             .get_real_value = stub_get_real_value,
                             .call = stub_call};

/** @brief Process-independent namespace; properties cannot be added or replaced. */
static object_t exceptions = {.vtbl = &vtbl};

object_t *get_exceptions_object() {
    return &exceptions;
}
