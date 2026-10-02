/**
 * @file common_methods.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Declarations of common methods for the Goat objects.
 */

#pragma once

#include "object.h"

/**
 * @brief Implements @ref object_vtbl_t::dec_ref, @ref object_vtbl_t::inc_ref, @ref
 * object_vtbl_t::mark, @ref object_vtbl_t::release.
 */
void stub_memory_function(object_t *obj);

/** @brief Implements @ref object_vtbl_t::sweep. */
bool no_sweep(object_t *obj);

/** @brief Implements @ref object_vtbl_t::compare. */
int compare_object_addresses(const object_t *obj1, const object_t *obj2);

/** @brief Implements @ref object_vtbl_t::clone. */
object_t *clone_singleton(process_t *process, object_t *obj);

/** @brief Implements @ref object_vtbl_t::to_string. */
string_value_t common_to_string(const object_t *obj);

/** @brief Implements @ref object_vtbl_t::to_string_notation. */
string_value_t common_to_string_notation(const object_t *obj);

/** @brief Implements @ref object_vtbl_t::get_prototypes. */
object_array_t common_get_prototypes(const object_t *obj);

/** @brief Implements @ref object_vtbl_t::get_topology. */
object_array_t common_get_topology(const object_t *obj);

/** @brief Implements @ref object_vtbl_t::create_property. */
model_status_t
create_property_on_immutable(object_t *obj, object_t *key, object_t *value, bool constant);

/** @brief Implements @ref object_vtbl_t::set_property. */
model_status_t set_property_on_immutable(object_t *obj, object_t *key, object_t *value);

/** @brief Implements @ref object_vtbl_t::add. */
operation_result_t stub_add(process_t *process, object_t *obj1, object_t *obj2);

/** @brief Implements @ref object_vtbl_t::subtract. */
operation_result_t stub_subtract(process_t *process, object_t *obj1, object_t *obj2);

/** @brief Implements @ref object_vtbl_t::multiply. */
operation_result_t stub_multiply(process_t *process, object_t *obj1, object_t *obj2);

/** @brief Implements @ref object_vtbl_t::divide. */
operation_result_t stub_divide(process_t *process, object_t *obj1, object_t *obj2);

/** @brief Implements @ref object_vtbl_t::modulo. */
operation_result_t stub_modulo(process_t *process, object_t *obj1, object_t *obj2);

/** @brief Implements @ref object_vtbl_t::power. */
operation_result_t stub_power(process_t *process, object_t *obj1, object_t *obj2);

/** @brief Implements @ref object_vtbl_t::less. */
bool common_less(const object_t *obj1, const object_t *obj2);

/** @brief Implements @ref object_vtbl_t::less_or_equal. */
bool common_less_or_equal(const object_t *obj1, const object_t *obj2);

/** @brief Implements @ref object_vtbl_t::greater. */
bool common_greater(const object_t *obj1, const object_t *obj2);

/** @brief Implements @ref object_vtbl_t::greater_or_equal. */
bool common_greater_or_equal(const object_t *obj1, const object_t *obj2);

/** @brief Implements @ref object_vtbl_t::equal. */
bool common_equal(const object_t *obj1, const object_t *obj2);

/** @brief Implements @ref object_vtbl_t::not_equal. */
bool common_not_equal(const object_t *obj1, const object_t *obj2);

/** @brief Implements @ref object_vtbl_t::get_boolean_value. */
bool common_get_boolean_value(const object_t *obj);

/** @brief Implements @ref object_vtbl_t::get_boolean_value. */
bool stub_get_boolean_value(const object_t *obj);

/** @brief Implements @ref object_vtbl_t::get_integer_value. */
int_value_t stub_get_integer_value(const object_t *obj);

/** @brief Implements @ref object_vtbl_t::get_real_value. */
real_value_t stub_get_real_value(const object_t *obj);

/** @brief Implements @ref object_vtbl_t::call for non-callable objects. */
bool stub_call(object_t *obj, uint16_t arg_count, thread_t *thread);
/** @brief Implements unsupported object_vtbl_t::unary_plus/unary_minus. */
operation_result_t stub_unary_operation(process_t *process, object_t *obj);
/** @brief Implements numeric object_vtbl_t::unary_plus, retaining the operand. */
operation_result_t numeric_unary_plus(process_t *process, object_t *obj);
