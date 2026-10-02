/**
 * @file null.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementations of the `null` object in the Goat programming language.
 */

#include "common_methods.h"
#include "object.h"
#include "process.h"

/** @brief Implements @ref object_vtbl_t::get_keys. */
static object_array_t get_keys(const object_t *obj) {
    return (object_array_t){NULL, 0};
}

/** @brief Implements @ref object_vtbl_t::get_property. */
static object_t *get_property(const object_t *obj, const object_t *key) {
    return NULL;
}

/** @brief Implements @ref object_vtbl_t::to_string. */
static string_value_t to_string(const object_t *obj) {
    return (string_value_t){L"null", 4, false};
}

/** @brief Implements @ref object_vtbl_t::to_string_notation. */
static string_value_t to_string_notation(const object_t *obj) {
    return to_string(obj);
}

/** @brief Implements @ref object_vtbl_t::get_boolean_value. */
static bool get_boolean_value(const object_t *obj) {
    return false;
}

/** @brief Virtual table defining the behavior of the `null` object. */
static object_vtbl_t vtbl = {.type = TYPE_OTHER,
                             .inc_ref = stub_memory_function,
                             .dec_ref = stub_memory_function,
                             .mark = stub_memory_function,
                             .sweep = no_sweep,
                             .release = stub_memory_function,
                             .compare = compare_object_addresses,
                             .clone = clone_singleton,
                             .to_string = to_string,
                             .to_string_notation = to_string_notation,
                             .get_prototypes = common_get_prototypes,
                             .get_topology = common_get_topology,
                             .get_keys = get_keys,
                             .get_property = get_property,
                             .create_property = create_property_on_immutable,
                             .set_property = set_property_on_immutable,
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
                             .get_boolean_value = get_boolean_value,
                             .get_integer_value = stub_get_integer_value,
                             .get_real_value = stub_get_real_value,
                             .call = stub_call};

/** @brief The singleton instance representing the `null` object. */
static object_t null_object = {.vtbl = &vtbl};

object_t *get_null_object() {
    return &null_object;
}
