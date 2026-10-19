/** @file integer_proto.c
 * @copyright 2026 Ivan Kniazkov
 * @brief The built-in Integer prototype.
 */
#include "common_methods.h"
#include "object.h"

/** @brief Implements @ref object_vtbl_t::get_prototypes. */
static object_array_t proto_get_prototypes(const object_t *obj) {
    static object_t *proto = NULL;
    if (!proto) {
        proto = get_numeric_proto();
    }
    object_array_t result = {.items = &proto, .size = 1};
    return result;
}

/** @brief Implements @ref object_vtbl_t::get_topology. */
static object_array_t proto_get_topology(const object_t *obj) {
    static object_t *topology[2] = {0};
    if (topology[0] == NULL) {
        topology[0] = get_numeric_proto();
        topology[1] = get_root_object();
    }
    object_array_t result = {.items = topology, .size = 2};
    return result;
}

/** @brief Virtual table defining the behavior of the integer prototype object. */
static object_vtbl_t integer_proto_vtbl = {.type = TYPE_OTHER,
                                           .inc_ref = stub_memory_function,
                                           .dec_ref = stub_memory_function,
                                           .mark = stub_memory_function,
                                           .sweep = no_sweep,
                                           .release = stub_memory_function,
                                           .compare = compare_object_addresses,
                                           .clone = clone_singleton,
                                           .to_string = common_to_string,
                                           .to_string_notation = common_to_string_notation,
                                           .get_prototypes = proto_get_prototypes,
                                           .get_topology = proto_get_topology,
                                           .get_keys = no_object_keys,
                                           .get_property = no_object_property,
                                           .create_property = create_property_on_immutable,
                                           .set_property = set_property_on_immutable,
                                           .get_index = common_get_index,
                                           .set_index = common_set_index,
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

/** @brief The integer prototype object. */
static object_t integer_proto = {.vtbl = &integer_proto_vtbl};

object_t *get_integer_proto() {
    return &integer_proto;
}
