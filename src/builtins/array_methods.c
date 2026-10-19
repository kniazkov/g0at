/** @file array_methods.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Goat-call adapters and descriptors for Array methods.
 */
#include "array_methods.h"

#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "model/array.h"
#include "model/builtin_function.h"
#include "model/thread.h"

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

/** @brief Validates arity/receiver before performing an operation. */
static operation_result_t
dispatch(array_method_t method, object_t *receiver, object_t **args, size_t count) {
    if (receiver->vtbl->type != TYPE_ARRAY)
        return operation_exception(get_exception_invalid_argument());
    size_t length = get_array_elements(receiver).size;
    size_t index, end;
    switch (method) {
        case PUSH:
            return insert_array_elements(receiver, length, args, count);
        case PREPEND:
            return insert_array_elements(receiver, 0, args, count);
        case INSERT:
            if (!count || !boundary(args[0], length, &index))
                break;
            return insert_array_elements(receiver, index, args + 1, count - 1);
        case POP:
            if (count)
                break;
            return length ? remove_array_element(receiver, length - 1)
                          : operation_success(get_null_object());
        case REMOVE:
            if (count != 1 || !length || !boundary(args[0], length - 1, &index))
                break;
            return remove_array_element(receiver, index);
        case CLEAR:
            if (count)
                break;
            return clear_array_elements(receiver);
        case INDEX_OF:
        case LAST_INDEX_OF:
            if (count != 1)
                break;
            return find_array_element(receiver, args[0], method == LAST_INDEX_OF);
        case SLICE:
            index = 0;
            end = length;
            if (count > 2 || (count && !boundary(args[0], length, &index))
                || (count > 1 && !boundary(args[1], length, &end)) || end < index)
                break;
            return slice_array(receiver, index, end);
        case REVERSE:
            if (count)
                break;
            return reverse_array_elements(receiver);
        case SORT:
            if (count)
                break;
            return sort_array_elements(receiver);
        case TO_STRING:
            if (count > 1)
                break;
            if (count && args[0]->vtbl->type != TYPE_STRING
                && args[0]->vtbl->type != TYPE_CHARACTER)
                break;
            return array_to_string(receiver, count ? args[0] : NULL);
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
