/**
 * @file common_methods.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implements common methods for the Goat objects.
 */

#include "common_methods.h"

#include "lib/allocate.h"
#include "lib/comparison.h"
#include "lib/string_ext.h"

#include <math.h>

void stub_memory_function(object_t *obj) {
    return;
}

bool no_sweep(object_t *obj) {
    return false;
}

int compare_object_addresses(const object_t *obj1, const object_t *obj2) {
    if ((uintptr_t)obj1 > (uintptr_t)obj2) {
        return 1;
    } else if ((uintptr_t)obj1 < (uintptr_t)obj2) {
        return -1;
    } else {
        return 0;
    }
}

object_t *clone_singleton(process_t *process, object_t *obj) {
    return obj;
}

string_value_t common_to_string(const object_t *obj) {
    return common_to_string_notation(obj);
}

string_value_t common_to_string_notation(const object_t *obj) {
    string_builder_t builder;
    init_string_builder(&builder, 2);
    append_char(&builder, '{');
    object_array_t keys = get_object_keys(obj);
    for (size_t index = 0; index < keys.size; index++) {
        if (index > 0) {
            append_char(&builder, ';');
        }
        object_t *key = keys.items[index];
        string_value_t key_str = convert_object_to_string_notation(key);
        append_string_value(&builder, key_str);
        FREE_STRING(key_str);
        append_char(&builder, '=');
        object_t *value = get_object_property(obj, key);
        string_value_t value_str = convert_object_to_string_notation(value);
        append_string_value(&builder, value_str);
        FREE_STRING(value_str);
    }
    return append_char(&builder, '}');
}

object_array_t common_get_prototypes(const object_t *obj) {
    static object_t *root_obj = NULL;
    if (!root_obj) {
        root_obj = get_root_object();
    }
    object_array_t result = {.items = &root_obj, .size = 1};
    return result;
}

object_array_t common_get_topology(const object_t *obj) {
    return common_get_prototypes(obj);
}

model_status_t
create_property_on_immutable(object_t *obj, object_t *key, object_t *value, bool constant) {
    return MSTAT_IMMUTABLE_OBJECT;
}

model_status_t set_property_on_immutable(object_t *obj, object_t *key, object_t *value) {
    return MSTAT_IMMUTABLE_OBJECT;
}

operation_result_t stub_add(process_t *process, object_t *obj1, object_t *obj2) {
    return operation_exception(get_exception_invalid_operation());
}

operation_result_t stub_subtract(process_t *process, object_t *obj1, object_t *obj2) {
    return operation_exception(get_exception_invalid_operation());
}

operation_result_t stub_multiply(process_t *process, object_t *obj1, object_t *obj2) {
    return operation_exception(get_exception_invalid_operation());
}

operation_result_t stub_divide(process_t *process, object_t *obj1, object_t *obj2) {
    return operation_exception(get_exception_invalid_operation());
}

operation_result_t stub_modulo(process_t *process, object_t *obj1, object_t *obj2) {
    return operation_exception(get_exception_invalid_operation());
}

operation_result_t stub_power(process_t *process, object_t *obj1, object_t *obj2) {
    return operation_exception(get_exception_invalid_operation());
}

/** @brief Compares numeric values without rounding integer operands. */
static comparison_order_t numeric_order(const object_t *left, const object_t *right) {
    bool li = is_integer_object(left), ri = is_integer_object(right);
    if (li && ri) {
        int64_t a = get_object_integer_value(left).value, b = get_object_integer_value(right).value;
        return a < b ? ORDER_LESS : a > b ? ORDER_GREATER : ORDER_EQUAL;
    }
    if (li)
        return compare_integer_real(get_object_integer_value(left).value,
                                    get_object_real_value(right).value);
    if (ri) {
        comparison_order_t order = compare_integer_real(get_object_integer_value(right).value,
                                                        get_object_real_value(left).value);
        return order == ORDER_LESS ? ORDER_GREATER : order == ORDER_GREATER ? ORDER_LESS : order;
    }
    return compare_reals(get_object_real_value(left).value, get_object_real_value(right).value);
}

int compare_numeric_keys(const object_t *left, const object_t *right) {
    comparison_order_t order = numeric_order(left, right);
    if (order == ORDER_UNORDERED) {
        bool a = isnan(get_object_real_value(left).value),
             b = isnan(get_object_real_value(right).value);
        return (int)a - (int)b;
    }
    return order == ORDER_LESS ? -1 : order == ORDER_GREATER ? 1 : 0;
}

static operation_result_t compare_values(object_t *left, object_t *right, comparison_kind_t kind) {
    bool equality = kind == COMPARE_EQUAL || kind == COMPARE_NOT_EQUAL;
    object_type_t lt = left->vtbl->type, rt = right->vtbl->type;
    bool ordered = lt == TYPE_NUMBER || lt == TYPE_STRING || lt == TYPE_BOOLEAN;
    comparison_order_t order;
    if (!ordered || lt != rt) {
        if (!equality)
            return operation_exception(ordered ? get_exception_invalid_argument()
                                               : get_exception_invalid_operation());
        order = left == right ? ORDER_EQUAL : ORDER_UNORDERED;
    } else if (lt == TYPE_NUMBER) {
        order = numeric_order(left, right);
    } else if (lt == TYPE_BOOLEAN) {
        bool a = get_object_boolean_value(left), b = get_object_boolean_value(right);
        order = a < b ? ORDER_LESS : a > b ? ORDER_GREATER : ORDER_EQUAL;
    } else {
        string_value_t a = convert_object_to_string(left), b = convert_object_to_string(right);
        order = compare_strings(VALUE_TO_VIEW(a), VALUE_TO_VIEW(b));
        FREE_STRING(a);
        FREE_STRING(b);
    }
    return (operation_result_t){get_boolean_object(comparison_matches(order, kind)), false};
}

operation_result_t common_less(process_t *process, object_t *obj1, object_t *obj2) {
    return compare_values(obj1, obj2, COMPARE_LESS);
}

operation_result_t common_less_or_equal(process_t *process, object_t *obj1, object_t *obj2) {
    return compare_values(obj1, obj2, COMPARE_LEQ);
}

operation_result_t common_greater(process_t *process, object_t *obj1, object_t *obj2) {
    return compare_values(obj1, obj2, COMPARE_GREATER);
}

operation_result_t common_greater_or_equal(process_t *process, object_t *obj1, object_t *obj2) {
    return compare_values(obj1, obj2, COMPARE_GREQ);
}

operation_result_t common_equal(process_t *process, object_t *obj1, object_t *obj2) {
    return compare_values(obj1, obj2, COMPARE_EQUAL);
}

operation_result_t common_not_equal(process_t *process, object_t *obj1, object_t *obj2) {
    return compare_values(obj1, obj2, COMPARE_NOT_EQUAL);
}

bool common_get_boolean_value(const object_t *obj) {
    return get_object_keys(obj).size > 0;
}

bool stub_get_boolean_value(const object_t *obj) {
    return true;
}

int_value_t stub_get_integer_value(const object_t *obj) {
    return (int_value_t){false, 0};
}

real_value_t stub_get_real_value(const object_t *obj) {
    return (real_value_t){false, 0.0};
}

bool stub_call(object_t *obj, uint16_t arg_count, thread_t *thread) {
    return false;
}

operation_result_t stub_unary_operation(process_t *process, object_t *obj) {
    return operation_exception(get_exception_invalid_operation());
}

operation_result_t numeric_unary_plus(process_t *process, object_t *obj) {
    INCREF(obj);
    return operation_success(obj);
}
