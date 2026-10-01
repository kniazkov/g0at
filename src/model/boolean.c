/**
 * @file boolean.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementations of two singleton objects representing the boolean values `true` and
 * `false`.
 */

#include "object.h"
#include "process.h"
#include "common_methods.h"

/** @brief Implements @ref object_vtbl_t::get_keys. */
static object_array_t get_keys(const object_t *obj) {
    return (object_array_t){ NULL, 0 };
}

/** @brief Implements @ref object_vtbl_t::get_property. */
static object_t *get_property(const object_t *obj, const object_t *key) {
    return NULL;
}

/** @brief Virtual table defining the behavior of the boolean prototype object. */
static object_vtbl_t boolean_proto_vtbl = {
    .type = TYPE_OTHER,
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
    .call = stub_call
};

/** @brief The boolean prototype object. */
static object_t boolean_proto = {
    .vtbl = &boolean_proto_vtbl
};

object_t *get_boolean_proto() {
    return &boolean_proto;
}

/** @brief Implements @ref object_vtbl_t::compare. */
static int compare(const object_t *obj1, const object_t *obj2) {
    return (get_object_boolean_value(obj1) ? 1 : 0)
        - (get_object_boolean_value(obj2) ? 1 : 0);
}

/** @brief Implements @ref object_vtbl_t::to_string. */
static string_value_t false_to_string(const object_t *obj) {
    return (string_value_t){ L"false", 5, false };
}

/** @brief Implements @ref object_vtbl_t::to_string_notation. */
static string_value_t false_to_string_notation(const object_t *obj) {
    return false_to_string(obj);
}

/** @brief Implements @ref object_vtbl_t::to_string. */
static string_value_t true_to_string(const object_t *obj) {
    return (string_value_t){ L"true", 4, false };
}

/** @brief Implements @ref object_vtbl_t::to_string_notation. */
static string_value_t true_to_string_notation(const object_t *obj) {
    return true_to_string(obj);
}

/** @brief Array of prototypes for the boolean object. */
static object_t* prototypes[] = {
    &boolean_proto
};

/** @brief Implements @ref object_vtbl_t::get_prototypes. */
static object_array_t get_prototypes(const object_t *obj) {
    object_array_t result = {
        .items = prototypes,
        .size = 1
    };
    return result;
}

/** @brief Implements @ref object_vtbl_t::get_topology. */
static object_array_t get_topology(const object_t *obj) {
    static object_t* topology[2] = {0};
    if (topology[0] == NULL) {
        topology[0] = &boolean_proto;
        topology[1] = get_root_object();
    }
    object_array_t result = {
        .items = topology,
        .size = 2
    };
    return result;
}

/** @brief Implements @ref object_vtbl_t::get_boolean_value. */
static bool false_get_boolean_value(const object_t *obj) {
    return false;
}

/** @brief Implements @ref object_vtbl_t::get_boolean_value. */
static bool true_get_boolean_value(const object_t *obj) {
    return true;
}

/** @brief Virtual table defining the behavior of the `false` object. */
static object_vtbl_t false_vtbl = {
    .type = TYPE_BOOLEAN,
    .inc_ref = stub_memory_function,
    .dec_ref = stub_memory_function,
    .mark = stub_memory_function,
    .sweep = no_sweep,
    .release = stub_memory_function,
    .compare = compare,
    .clone = clone_singleton,
    .to_string = false_to_string,
    .to_string_notation = false_to_string_notation,
    .get_prototypes = get_prototypes,
    .get_topology = get_topology,
    .get_keys = get_keys,
    .get_property = get_property,
    .create_property = create_property_on_immutable,
    .set_property = set_property_on_immutable,
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
    .get_boolean_value = false_get_boolean_value,
    .get_integer_value = stub_get_integer_value,
    .get_real_value = stub_get_real_value,
    .call = stub_call
};

/** @brief The singleton instance representing the boolean value `false`. */
static object_t false_object = {
    .vtbl = &false_vtbl
};

/** @brief Virtual table defining the behavior of the `true` object. */
static object_vtbl_t true_vtbl = {
    .type = TYPE_BOOLEAN,
    .inc_ref = stub_memory_function,
    .dec_ref = stub_memory_function,
    .mark = stub_memory_function,
    .sweep = no_sweep,
    .release = stub_memory_function,
    .compare = compare,
    .clone = clone_singleton,
    .to_string = true_to_string,
    .to_string_notation = true_to_string_notation,
    .get_prototypes = get_prototypes,
    .get_topology = get_topology,
    .get_keys = get_keys,
    .get_property = get_property,
    .create_property = create_property_on_immutable,
    .set_property = set_property_on_immutable,
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
    .get_boolean_value = true_get_boolean_value,
    .get_integer_value = stub_get_integer_value,
    .get_real_value = stub_get_real_value,
    .call = stub_call
};

/** @brief The singleton instance representing the boolean value `true`. */
static object_t true_object = {
    .vtbl = &true_vtbl
};

object_t *get_boolean_object(bool value) {
    return value ? &true_object : &false_object;
}
