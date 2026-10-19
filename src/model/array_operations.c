/** @file array_operations.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Array storage algorithms and ownership, independent of Goat call argument lists.
 */
#include "array.h"
#include "array_internal.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/** @brief Returns the retained receiver for in-place operations. */
static operation_result_t self(object_t *obj) {
    INCREF(obj);
    return operation_success(obj);
}

/** @brief Inserts borrowed argument values, preserving order and self references. */
operation_result_t
insert_array_elements(object_t *obj, size_t index, object_t **args, size_t count) {
    array_object_t *array = (array_object_t *)obj;
    if (count > SIZE_MAX - array->count)
        abort();
    size_t length = array->count + count;
    if (count) {
        reserve_array(array, length);
        memmove(array->items + index + count,
                array->items + index,
                (array->count - index) * sizeof(*array->items));
        for (size_t i = 0; i < count; i++) {
            INCREF(args[i]);
            array->items[index + i] = args[i];
        }
        set_array_count(array, length);
    }
    INCREF(array->length);
    return operation_success(array->length);
}

/** @brief Transfers the array's reference to the removed element to the caller. */
operation_result_t remove_array_element(object_t *obj, size_t index) {
    array_object_t *array = (array_object_t *)obj;
    object_t *value = array->items[index];
    memmove(array->items + index,
            array->items + index + 1,
            (array->count - index - 1) * sizeof(*array->items));
    array->items[array->count - 1] = NULL;
    set_array_count(array, array->count - 1);
    return operation_success(value);
}

/** @brief Clears storage before releasing values, including references to the receiver. */
operation_result_t clear_array_elements(object_t *obj) {
    array_object_t *array = (array_object_t *)obj;
    object_t **items = array->items;
    size_t count = array->count;
    array->items = NULL;
    array->capacity = 0;
    set_array_count(array, 0);
    for (size_t i = 0; i < count; i++)
        DECREF(items[i]);
    FREE(items);
    return self(&array->base);
}

/** @brief Equality uses the same virtual operation as Goat's == operator. */
operation_result_t find_array_element(object_t *obj, object_t *value, bool last) {
    array_object_t *array = (array_object_t *)obj;
    for (size_t i = 0; i < array->count; i++) {
        size_t index = last ? array->count - 1 - i : i;
        operation_result_t result =
            are_objects_equal(array->base.process, array->items[index], value);
        if (result.is_exception)
            return result;
        bool equal = get_object_boolean_value(result.value);
        DECREF(result.value);
        if (equal)
            return operation_success(create_integer_object(array->base.process, (int64_t)index));
    }
    return operation_success(get_static_integer_object(-1));
}

/** @brief Stable merge sort of borrowed pointers; original storage changes only on success. */
operation_result_t sort_array_elements(object_t *obj) {
    array_object_t *array = (array_object_t *)obj;
    size_t count = array->count;
    if (count < 2)
        return self(&array->base);
    object_t **source = ALLOC(count * sizeof(*source));
    object_t **target = ALLOC(count * sizeof(*target));
    memcpy(source, array->items, count * sizeof(*source));
    for (size_t width = 1; width<count; width = width> count / 2 ? count : width * 2) {
        for (size_t begin = 0; begin < count;) {
            size_t middle = begin + (count - begin < width ? count - begin : width);
            size_t end = middle + (count - middle < width ? count - middle : width);
            size_t left = begin, right = middle, output = begin;
            while (left < middle && right < end) {
                operation_result_t result =
                    is_object_less_than(array->base.process, source[right], source[left]);
                if (result.is_exception) {
                    FREE(source);
                    FREE(target);
                    return result;
                }
                bool less = get_object_boolean_value(result.value);
                DECREF(result.value);
                target[output++] = less ? source[right++] : source[left++];
            }
            while (left < middle)
                target[output++] = source[left++];
            while (right < end)
                target[output++] = source[right++];
            begin = end;
        }
        object_t **swap = source;
        source = target;
        target = swap;
    }
    memcpy(array->items, source, count * sizeof(*source));
    FREE(source);
    FREE(target);
    return self(&array->base);
}

/** @brief Without a separator, preserve array notation; with one, join plain element strings. */
operation_result_t array_to_string(object_t *obj, object_t *separator_object) {
    array_object_t *array = (array_object_t *)obj;
    if (!separator_object)
        return operation_success(
            create_string_object(array->base.process, container_to_string_notation(&array->base)));
    string_value_t separator = convert_object_to_string(separator_object);
    string_builder_t builder = {0};
    string_value_t text = STATIC_STRING(L"");
    for (size_t i = 0; i < array->count; i++) {
        if (i)
            append_string_value(&builder, separator);
        string_value_t value = convert_object_to_string(array->items[i]);
        text = append_string_value(&builder, value);
        FREE_STRING(value);
    }
    FREE_STRING(separator);
    return operation_success(create_string_object(array->base.process, text));
}

operation_result_t slice_array(object_t *obj, size_t begin, size_t end) {
    array_object_t *array = (array_object_t *)obj;
    return operation_success(create_array_object(
        obj->process,
        (object_array_t){end > begin ? array->items + begin : NULL, end - begin}));
}

operation_result_t reverse_array_elements(object_t *obj) {
    array_object_t *array = (array_object_t *)obj;
    for (size_t i = 0; i < array->count / 2; i++) {
        object_t *value = array->items[i];
        array->items[i] = array->items[array->count - 1 - i];
        array->items[array->count - 1 - i] = value;
    }
    return self(obj);
}
