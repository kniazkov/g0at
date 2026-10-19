/** @file array.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Mutable arrays, owned elements and tracing garbage collection.
 */
#include "array.h"

#include "common_methods.h"
#include "lib/allocate.h"
#include "object_state.h"
#include "process.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/** @brief Releases storage without touching other objects during sweeping/shutdown. */
static void release(object_t *obj) {
    array_object_t *array = (array_object_t *)obj;
    remove_object_from_list(&obj->process->objects, obj);
    FREE(array->items);
    FREE(array);
}

/** @brief Retains an array. */
static void inc_ref(object_t *obj) {
    ((array_object_t *)obj)->refs++;
}

/** @brief Releases owned values when the last reference disappears. */
static void dec_ref(object_t *obj) {
    array_object_t *array = (array_object_t *)obj;
    if (array->state == DYING || --array->refs)
        return;
    array->state = DYING;
    for (size_t i = 0; i < array->count; i++)
        DECREF(array->items[i]);
    DECREF(array->length);
    release(obj);
}

/** @brief Marks each element once, including cycles through other containers. */
static void mark(object_t *obj) {
    array_object_t *array = (array_object_t *)obj;
    if (array->state != UNMARKED)
        return;
    array->state = MARKED;
    mark_object(array->length);
    for (size_t i = 0; i < array->count; i++)
        mark_object(array->items[i]);
}

/** @brief Sweeps unreachable arrays without dereferencing already swept children. */
static bool sweep(object_t *obj) {
    array_object_t *array = (array_object_t *)obj;
    if (array->state == UNMARKED) {
        release(obj);
        return true;
    }
    array->state = UNMARKED;
    return false;
}

object_array_t get_array_elements(const object_t *obj) {
    assert(obj->vtbl->type == TYPE_ARRAY);
    const array_object_t *array = (const array_object_t *)obj;
    return (object_array_t){array->items, array->count};
}

/** @brief A shallow independent copy; direct self references target the copy. */
static object_t *clone(process_t *process, object_t *obj) {
    array_object_t *copy = (array_object_t *)create_array_object(process, get_array_elements(obj));
    for (size_t i = 0; i < copy->count; i++) {
        if (copy->items[i] == obj) {
            INCREF(copy);
            copy->items[i] = &copy->base;
            DECREF(obj);
        }
    }
    return &copy->base;
}

/** @brief Arrays expose length as a named property, separate from indexed elements. */
static object_array_t get_keys(const object_t *obj) {
    static object_t *length_key;
    if (!length_key)
        length_key = get_string_length();
    return (object_array_t){&length_key, 1};
}

/** @brief Returns a borrowed immutable length value. */
static object_t *get_property(const object_t *obj, const object_t *key) {
    if (key->vtbl->type == TYPE_STRING && compare_objects_using_vtbl(key, get_string_length()) == 0)
        return ((const array_object_t *)obj)->length;
    return NULL;
}

/** @brief Length is constant; arrays do not carry arbitrary named properties. */
static model_status_t set_property(object_t *obj, object_t *key, object_t *value) {
    return get_property(obj, key) ? MSTAT_PROPERTY_IS_CONSTANT : MSTAT_IMMUTABLE_OBJECT;
}

/** @brief Checks arity, representation, sign and bounds without lossy conversion. */
static bool index_value(const array_object_t *array, object_array_t indices, size_t *index) {
    if (indices.size != 1 || !is_integer_object(indices.items[0]))
        return false;
    int64_t value = get_object_integer_value(indices.items[0]).value;
    if (value < 0 || (uint64_t)value >= array->count)
        return false;
    *index = (size_t)value;
    return true;
}

/** @brief Reads an element and returns an owned reference. */
static operation_result_t get_index(process_t *process, object_t *obj, object_array_t indices) {
    array_object_t *array = (array_object_t *)obj;
    size_t index;
    if (!index_value(array, indices, &index))
        return operation_exception(get_exception_invalid_argument());
    object_t *value = array->items[index];
    INCREF(value);
    return operation_success(value);
}

/** @brief Retains a replacement before releasing the old value (alias-safe). */
static operation_result_t
set_index(process_t *process, object_t *obj, object_array_t indices, object_t *value) {
    array_object_t *array = (array_object_t *)obj;
    size_t index;
    if (!index_value(array, indices, &index))
        return operation_exception(get_exception_invalid_argument());
    INCREF(value);
    object_t *old = array->items[index];
    array->items[index] = value;
    DECREF(old);
    INCREF(value);
    return operation_success(value);
}

/** @brief Empty arrays are false, nonempty arrays are true. */
static bool get_boolean_value(const object_t *obj) {
    return ((const array_object_t *)obj)->count != 0;
}

/** @brief Every array directly inherits from the singleton Array prototype. */
static object_array_t get_prototypes(const object_t *obj) {
    static object_t *prototype;
    if (!prototype)
        prototype = get_array_proto();
    return (object_array_t){&prototype, 1};
}

/** @brief Full lookup order is Array, then Object. */
static object_array_t get_topology(const object_t *obj) {
    static object_t *topology[2];
    if (!topology[0]) {
        topology[0] = get_array_proto();
        topology[1] = get_root_object();
    }
    return (object_array_t){topology, 2};
}

/** @brief Array behavior; indexed access is independent of named properties. */
static object_vtbl_t vtbl = {.type = TYPE_ARRAY,
                             .inc_ref = inc_ref,
                             .dec_ref = dec_ref,
                             .mark = mark,
                             .sweep = sweep,
                             .release = release,
                             .compare = compare_object_addresses,
                             .clone = clone,
                             .to_string = container_to_string_notation,
                             .to_string_notation = container_to_string_notation,
                             .get_prototypes = get_prototypes,
                             .get_topology = get_topology,
                             .get_keys = get_keys,
                             .get_property = get_property,
                             .create_property = create_property_on_immutable,
                             .set_property = set_property,
                             .get_index = get_index,
                             .set_index = set_index,
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
                             .bitwise_not = stub_unary_operation,
                             .bitwise_and = stub_bitwise,
                             .bitwise_or = stub_bitwise,
                             .bitwise_xor = stub_bitwise,
                             .shift_left = stub_bitwise,
                             .shift_right = stub_bitwise,
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

object_t *create_array_object(process_t *process, object_array_t elements) {
    /* Public constructor may be called outside the VM: guard size arithmetic too. */
    if (elements.size > SIZE_MAX / sizeof(object_t *) || elements.size > INT64_MAX)
        abort();
    array_object_t *array = CALLOC(sizeof(*array));
    array->base.vtbl = &vtbl;
    array->base.process = process;
    array->refs = 1;
    array->count = elements.size;
    array->capacity = elements.size;
    array->length = create_integer_object(process, (int64_t)elements.size);
    if (elements.size) {
        array->items = ALLOC(elements.size * sizeof(*array->items));
        for (size_t i = 0; i < elements.size; i++) {
            array->items[i] = elements.items[i];
            INCREF(array->items[i]);
        }
    }
    add_object_to_list(&process->objects, &array->base);
    return &array->base;
}

void set_array_count(array_object_t *array, size_t count) {
    object_t *length = create_integer_object(array->base.process, (int64_t)count);
    object_t *old = array->length;
    array->count = count;
    array->length = length;
    DECREF(old);
}

void reserve_array(array_object_t *array, size_t count) {
    size_t limit = SIZE_MAX / sizeof(object_t *);
    if ((uint64_t)limit > INT64_MAX)
        limit = (size_t)INT64_MAX;
    if (count > limit)
        abort();
    if (count <= array->capacity)
        return;
    size_t capacity = array->capacity ? array->capacity : 4;
    while (capacity < count) {
        if (capacity > limit / 2) {
            capacity = count;
            break;
        }
        capacity *= 2;
    }
    object_t **items = ALLOC(capacity * sizeof(*items));
    if (array->count)
        memcpy(items, array->items, array->count * sizeof(*items));
    FREE(array->items);
    array->items = items;
    array->capacity = capacity;
}
