/** @file array_methods.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Built-in Array operations with owned results and exception-safe sorting.
 */
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "array.h"
#include "builtin_function.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"
#include "thread.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/** @brief Operations provided by the Array prototype. */
typedef enum {
    PUSH,
    PREPEND,
    INSERT,
    POP,
    REMOVE,
    CLEAR,
    INDEX_OF,
    LAST_INDEX_OF,
    SLICE,
    REVERSE,
    SORT,
    TO_STRING
} array_method_t;

/** @brief Returns the retained receiver for in-place operations. */
static operation_result_t self(object_t *obj) {
    INCREF(obj);
    return operation_success(obj);
}

/** @brief Validates a nonnegative integer boundary without narrowing first. */
static bool boundary(object_t *obj, size_t limit, size_t *value) {
    if (!is_integer_object(obj))
        return false;
    int64_t integer = get_object_integer_value(obj).value;
    if (integer < 0 || (uint64_t)integer > limit)
        return false;
    *value = (size_t)integer;
    return true;
}

/** @brief Inserts borrowed argument values, preserving order and self references. */
static operation_result_t
insert(array_object_t *array, size_t index, object_t **args, size_t count) {
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
static operation_result_t remove_at(array_object_t *array, size_t index) {
    object_t *value = array->items[index];
    memmove(array->items + index,
            array->items + index + 1,
            (array->count - index - 1) * sizeof(*array->items));
    array->items[array->count - 1] = NULL;
    set_array_count(array, array->count - 1);
    return operation_success(value);
}

/** @brief Clears storage before releasing values, including references to the receiver. */
static operation_result_t clear(array_object_t *array) {
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
static operation_result_t find(array_object_t *array, object_t *value, bool last) {
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
static operation_result_t sort(array_object_t *array) {
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
static operation_result_t to_string(array_object_t *array, object_t **args, size_t count) {
    if (!count)
        return operation_success(
            create_string_object(array->base.process, container_to_string_notation(&array->base)));
    if (args[0]->vtbl->type != TYPE_STRING && args[0]->vtbl->type != TYPE_CHARACTER)
        return operation_exception(get_exception_invalid_argument());
    string_value_t separator = convert_object_to_string(args[0]);
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

/** @brief Validates arity/receiver before performing an operation. */
static operation_result_t
dispatch(array_method_t method, object_t *receiver, object_t **args, size_t count) {
    if (receiver->vtbl->type != TYPE_ARRAY)
        return operation_exception(get_exception_invalid_argument());
    array_object_t *array = (array_object_t *)receiver;
    size_t index, end;
    switch (method) {
        case PUSH:
            return insert(array, array->count, args, count);
        case PREPEND:
            return insert(array, 0, args, count);
        case INSERT:
            if (!count || !boundary(args[0], array->count, &index))
                break;
            return insert(array, index, args + 1, count - 1);
        case POP:
            if (count)
                break;
            return array->count ? remove_at(array, array->count - 1)
                                : operation_success(get_null_object());
        case REMOVE:
            if (count != 1 || !array->count || !boundary(args[0], array->count - 1, &index))
                break;
            return remove_at(array, index);
        case CLEAR:
            if (count)
                break;
            return clear(array);
        case INDEX_OF:
        case LAST_INDEX_OF:
            if (count != 1)
                break;
            return find(array, args[0], method == LAST_INDEX_OF);
        case SLICE:
            index = 0;
            end = array->count;
            if (count > 2 || (count && !boundary(args[0], array->count, &index))
                || (count > 1 && !boundary(args[1], array->count, &end)) || end < index)
                break;
            return operation_success(create_array_object(
                array->base.process,
                (object_array_t){end > index ? array->items + index : NULL, end - index}));
        case REVERSE:
            if (count)
                break;
            for (size_t i = 0; i < array->count / 2; i++) {
                object_t *value = array->items[i];
                array->items[i] = array->items[array->count - 1 - i];
                array->items[array->count - 1 - i] = value;
            }
            return self(receiver);
        case SORT:
            if (count)
                break;
            return sort(array);
        case TO_STRING:
            if (count > 1)
                break;
            return to_string(array, args, count);
    }
    return operation_exception(get_exception_invalid_argument());
}

/** @brief Mutations and unknown virtual comparisons must not leave stale abstract facts. */
static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    forget_abstract_values(state);
    return make_top_element();
}

/** @brief Receiver-aware push entry point. */
static operation_result_t
method_push(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    return dispatch(PUSH, receiver, args, count);
}

/** @brief Detached push entry point with the receiver as its first argument. */
static operation_result_t plain_push(object_t **args, uint16_t count, thread_t *thread) {
    if (!count)
        return operation_exception(get_exception_invalid_argument());
    return dispatch(PUSH, args[0], args + 1, count - 1);
}

/** @brief Receiver-aware prepend entry point. */
static operation_result_t
method_prepend(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    return dispatch(PREPEND, receiver, args, count);
}

/** @brief Detached prepend entry point with the receiver as its first argument. */
static operation_result_t plain_prepend(object_t **args, uint16_t count, thread_t *thread) {
    if (!count)
        return operation_exception(get_exception_invalid_argument());
    return dispatch(PREPEND, args[0], args + 1, count - 1);
}

/** @brief Receiver-aware insert entry point. */
static operation_result_t
method_insert(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    return dispatch(INSERT, receiver, args, count);
}

/** @brief Detached insert entry point with the receiver as its first argument. */
static operation_result_t plain_insert(object_t **args, uint16_t count, thread_t *thread) {
    if (!count)
        return operation_exception(get_exception_invalid_argument());
    return dispatch(INSERT, args[0], args + 1, count - 1);
}

/** @brief Receiver-aware pop entry point. */
static operation_result_t
method_pop(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    return dispatch(POP, receiver, args, count);
}

/** @brief Detached pop entry point with the receiver as its first argument. */
static operation_result_t plain_pop(object_t **args, uint16_t count, thread_t *thread) {
    if (!count)
        return operation_exception(get_exception_invalid_argument());
    return dispatch(POP, args[0], args + 1, count - 1);
}

/** @brief Receiver-aware remove entry point. */
static operation_result_t
method_remove(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    return dispatch(REMOVE, receiver, args, count);
}

/** @brief Detached remove entry point with the receiver as its first argument. */
static operation_result_t plain_remove(object_t **args, uint16_t count, thread_t *thread) {
    if (!count)
        return operation_exception(get_exception_invalid_argument());
    return dispatch(REMOVE, args[0], args + 1, count - 1);
}

/** @brief Receiver-aware clear entry point. */
static operation_result_t
method_clear(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    return dispatch(CLEAR, receiver, args, count);
}

/** @brief Detached clear entry point with the receiver as its first argument. */
static operation_result_t plain_clear(object_t **args, uint16_t count, thread_t *thread) {
    if (!count)
        return operation_exception(get_exception_invalid_argument());
    return dispatch(CLEAR, args[0], args + 1, count - 1);
}

/** @brief Receiver-aware indexOf entry point. */
static operation_result_t
method_indexOf(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    return dispatch(INDEX_OF, receiver, args, count);
}

/** @brief Detached indexOf entry point with the receiver as its first argument. */
static operation_result_t plain_indexOf(object_t **args, uint16_t count, thread_t *thread) {
    if (!count)
        return operation_exception(get_exception_invalid_argument());
    return dispatch(INDEX_OF, args[0], args + 1, count - 1);
}

/** @brief Receiver-aware lastIndexOf entry point. */
static operation_result_t
method_lastIndexOf(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    return dispatch(LAST_INDEX_OF, receiver, args, count);
}

/** @brief Detached lastIndexOf entry point with the receiver as its first argument. */
static operation_result_t plain_lastIndexOf(object_t **args, uint16_t count, thread_t *thread) {
    if (!count)
        return operation_exception(get_exception_invalid_argument());
    return dispatch(LAST_INDEX_OF, args[0], args + 1, count - 1);
}

/** @brief Receiver-aware slice entry point. */
static operation_result_t
method_slice(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    return dispatch(SLICE, receiver, args, count);
}

/** @brief Detached slice entry point with the receiver as its first argument. */
static operation_result_t plain_slice(object_t **args, uint16_t count, thread_t *thread) {
    if (!count)
        return operation_exception(get_exception_invalid_argument());
    return dispatch(SLICE, args[0], args + 1, count - 1);
}

/** @brief Receiver-aware reverse entry point. */
static operation_result_t
method_reverse(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    return dispatch(REVERSE, receiver, args, count);
}

/** @brief Detached reverse entry point with the receiver as its first argument. */
static operation_result_t plain_reverse(object_t **args, uint16_t count, thread_t *thread) {
    if (!count)
        return operation_exception(get_exception_invalid_argument());
    return dispatch(REVERSE, args[0], args + 1, count - 1);
}

/** @brief Receiver-aware sort entry point. */
static operation_result_t
method_sort(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    return dispatch(SORT, receiver, args, count);
}

/** @brief Detached sort entry point with the receiver as its first argument. */
static operation_result_t plain_sort(object_t **args, uint16_t count, thread_t *thread) {
    if (!count)
        return operation_exception(get_exception_invalid_argument());
    return dispatch(SORT, args[0], args + 1, count - 1);
}

/** @brief Receiver-aware toString entry point. */
static operation_result_t
method_toString(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    return dispatch(TO_STRING, receiver, args, count);
}

/** @brief Detached toString entry point with the receiver as its first argument. */
static operation_result_t plain_toString(object_t **args, uint16_t count, thread_t *thread) {
    if (!count)
        return operation_exception(get_exception_invalid_argument());
    return dispatch(TO_STRING, args[0], args + 1, count - 1);
}

/** @brief Descriptors are scoped to Array, independent of same-named String methods. */
static const builtin_function_t methods[] = {
    {.name = L"push",
     .min_args = 0,
     .effects = BUILTIN_EFFECT_BINDINGS,
     .execute = plain_push,
     .execute_method = method_push,
     .interpret = interpret},
    {.name = L"prepend",
     .min_args = 0,
     .effects = BUILTIN_EFFECT_BINDINGS,
     .execute = plain_prepend,
     .execute_method = method_prepend,
     .interpret = interpret},
    {.name = L"insert",
     .min_args = 0,
     .effects = BUILTIN_EFFECT_BINDINGS,
     .execute = plain_insert,
     .execute_method = method_insert,
     .interpret = interpret},
    {.name = L"pop",
     .min_args = 0,
     .effects = BUILTIN_EFFECT_BINDINGS,
     .execute = plain_pop,
     .execute_method = method_pop,
     .interpret = interpret},
    {.name = L"remove",
     .min_args = 0,
     .effects = BUILTIN_EFFECT_BINDINGS,
     .execute = plain_remove,
     .execute_method = method_remove,
     .interpret = interpret},
    {.name = L"clear",
     .min_args = 0,
     .effects = BUILTIN_EFFECT_BINDINGS,
     .execute = plain_clear,
     .execute_method = method_clear,
     .interpret = interpret},
    {.name = L"indexOf",
     .min_args = 0,
     .effects = BUILTIN_EFFECT_BINDINGS,
     .execute = plain_indexOf,
     .execute_method = method_indexOf,
     .interpret = interpret},
    {.name = L"lastIndexOf",
     .min_args = 0,
     .effects = BUILTIN_EFFECT_BINDINGS,
     .execute = plain_lastIndexOf,
     .execute_method = method_lastIndexOf,
     .interpret = interpret},
    {.name = L"slice",
     .min_args = 0,
     .effects = BUILTIN_EFFECT_BINDINGS,
     .execute = plain_slice,
     .execute_method = method_slice,
     .interpret = interpret},
    {.name = L"reverse",
     .min_args = 0,
     .effects = BUILTIN_EFFECT_BINDINGS,
     .execute = plain_reverse,
     .execute_method = method_reverse,
     .interpret = interpret},
    {.name = L"sort",
     .min_args = 0,
     .effects = BUILTIN_EFFECT_BINDINGS,
     .execute = plain_sort,
     .execute_method = method_sort,
     .interpret = interpret},
    {.name = L"toString",
     .min_args = 0,
     .effects = BUILTIN_EFFECT_BINDINGS,
     .execute = plain_toString,
     .execute_method = method_toString,
     .interpret = interpret},
};

object_array_t get_array_method_keys(void) {
    enum { count = sizeof(methods) / sizeof(*methods) };

    static object_static_string_t names[count];
    static object_t *keys[count];
    if (!keys[0]) {
        for (size_t i = 0; i < count; i++)
            keys[i] = get_static_string_object(&names[i], methods[i].name);
    }
    return (object_array_t){keys, count};
}

object_t *get_array_method(const object_t *key) {
    if (key->vtbl->type != TYPE_STRING)
        return NULL;

    enum { count = sizeof(methods) / sizeof(*methods) };

    static builtin_function_object_t objects[count];
    object_array_t names = get_array_method_keys();
    for (size_t i = 0; i < count; i++)
        if (!compare_objects_using_vtbl(key, names.items[i]))
            return get_builtin_function_object(&objects[i], &methods[i]);
    return NULL;
}
