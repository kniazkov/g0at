/**
 * @file common_methods.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implements common methods for the Goat objects.
 * 
 * This file provides implementations of methods that are shared across multiple 
 * object implementations in the Goat language. These methods are defined separately 
 * to ensure code reuse and maintainability.
 */

#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

void stub_memory_function(object_t *obj) {
    return;
}

int compare_object_addresses(const object_t *obj1, const object_t *obj2) {
    if (obj1 > obj2) {
        return 1;
    } else if (obj1 < obj2) {
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
    object_array_t keys = obj->vtbl->get_keys(obj);
    for (size_t index = 0; index < keys.size; index++) {
        if (index > 0) {
            append_char(&builder, ';');
        }
        object_t *key = keys.items[index];
        string_value_t key_str = key->vtbl->to_string_notation(key);
        append_substring(&builder, key_str.data, key_str.length);
        if (key_str.should_free) {
            FREE(key_str.data);
        }
        append_char(&builder, '=');
        object_t *value = obj->vtbl->get_property(obj, key);
        string_value_t value_str = value->vtbl->to_string_notation(value);
        append_substring(&builder, value_str.data, value_str.length);
        if (value_str.should_free) {
            FREE(value_str.data);
        }
    }
    return append_char(&builder, '}');
}

object_array_t common_get_prototypes(const object_t *obj) {
    static object_t *root_obj = NULL;
    if (!root_obj) {
        root_obj = get_root_object();
    }
    object_array_t result = {
        .items = &root_obj,
        .size = 1
    };
    return result;
}

object_array_t common_get_topology(const object_t *obj) {
    return common_get_prototypes(obj);
}

bool add_property_on_immutable(object_t *obj, object_t *key, object_t *value, bool constant) {
    return false;
}

bool set_property_on_immutable(object_t *obj, object_t *key, object_t *value) {
    return false;
}

object_t *stub_add(process_t *process, object_t *obj1, object_t *obj2) {
    return NULL;
}

object_t *stub_sub(process_t *process, object_t *obj1, object_t *obj2) {
    return NULL;
}

bool common_get_boolean_value(const object_t *obj) {
    return obj->vtbl->get_keys(obj).size > 0;
}

bool stub_get_boolean_value(const object_t *obj) {
    return true;
}

int_value_t stub_get_integer_value(const object_t *obj) {
    return (int_value_t){ false, 0 };
}

real_value_t stub_get_real_value(const object_t *obj) {
    return (real_value_t){ false, 0.0 };
}

bool stub_call(object_t *obj, uint16_t arg_count, thread_t *thread) {
    return false;
}
