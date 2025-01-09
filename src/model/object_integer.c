/**
 * @file object_integer.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementations of an object representing an integer.
 *
 * This file defines the structure and behavior of integer objects. There are two types
 * of integer objects:
 * 1. Static integers:
 *    - These include integers declared in the model or constants used in bytecode.
 *    - Static integers only store a reference to their value, which exists for the duration
 *      of the program's execution. These integers are not managed by the garbage collector.
 * 2. Dynamic integers:
 *    - These are created as a result of operations at runtime.
 *    - They internally store their own value and are subject to garbage collection when
 *      no longer in use.
 */

#include <assert.h>
#include <stdio.h>
#include <inttypes.h>

#include "object.h"
#include "object_state.h"
#include "process.h"
#include "common_methods.h"
#include "lib/allocate.h"

/**
 * @def POOL_CAPACITY
 * @brief Defines the maximum capacity of the object pool.
 * 
 * This macro sets the maximum number of objects that can be stored in the object pool 
 * before it reaches its capacity. Once the pool is full, any swept objects are destroyed 
 * instead of being added to the pool.
 */
#define POOL_CAPACITY 1024

/**
 * @struct object_static_integer_t
 * @brief Structure representing a static integer object.
 */
typedef struct {
    object_t base; ///< The base object that provides common functionality.
    int64_t value; ///< The integer value of the object.
} object_static_integer_t;

/**
 * @struct object_dynamic_integer_t
 * @brief Structure representing a dynamic integer object.
 */
typedef struct {
    object_t base; ///< The base object that provides common functionality.
    int refs; ///< Reference count.
    object_state_t state; ///< The state of the object (e.g., unmarked, marked, or zombie).
    int64_t value; ///< The integer value of the object.
} object_dynamic_integer_t;

/**
 * @brief Retrieves the prototypes of the integer prototype object.
 * 
 * This function returns an array of prototypes the integer prototype object.
 * 
 * @param obj The object whose prototypes are to be retrieved.
 * @return An object_array_t containing the prototypes of the integer prototype object.
 */
static object_array_t proto_get_prototypes(const object_t *obj) {
    static object_t *proto = NULL;
    if (!proto) {
        proto = get_numeric_proto();
    }
    object_array_t result = {
        .items = &proto,
        .size = 1
    };
    return result;
}

/**
 * @brief Retrieves the full prototype topology of the integer prototype object.
 * 
 * This function returns the full prototype chain (topology) of the integer prototype object. 
 * 
 * @param obj The object whose prototype topology is to be retrieved.
 * @return An object_array_t containing the full prototype chain.
 */
static object_array_t proto_get_topology(const object_t *obj) {
    static object_t* topology[2] = {0};
    if (topology[0] == NULL) {
        topology[0] = get_numeric_proto();
        topology[1] = get_root_object();
    }
    object_array_t result = {
        .items = topology,
        .size = 2
    };
    return result;
}

/**
 * @brief Retrieves all property keys from an object (stub implementation).
 * 
 * This is a stub implementation of the function to retrieve all keys of the properties 
 * defined on an object. Currently, it returns an empty `object_array_t` as a placeholder.
 * 
 * @param obj The object from which to retrieve the keys.
 * @return An empty `object_array_t` (placeholder implementation).
 */
static object_array_t get_keys(const object_t *obj) {
    return (object_array_t){ NULL, 0 };
}

/**
 * @brief Retrieves the value of a property from an object (stub implementation).
 * 
 * This is a stub implementation of the function to retrieve the value of a property from
 * an object. Currently, it returns `NULL` as a placeholder.
 * 
 * @param obj The object from which to retrieve the property.
 * @param key The key of the property to retrieve.
 * @return Always returns `NULL` (placeholder implementation).
 */
static object_t *get_property(const object_t *obj, const object_t *key) {
    return NULL;
}

/**
 * @var integer_proto_vtbl
 * @brief Virtual table defining the behavior of the integer prototype object.
 */
static object_vtbl_t integer_proto_vtbl = {
    .type = TYPE_STRING,
    .inc_ref = stub_memory_function,
    .dec_ref = stub_memory_function,
    .mark = stub_memory_function,
    .sweep = stub_memory_function,
    .release = stub_memory_function,
    .compare = compare_object_addresses,
    .clone = clone_singleton,
    .to_string = common_to_string,
    .to_string_notation = common_to_string_notation,
    .get_prototypes = proto_get_prototypes,
    .get_topology = proto_get_topology,
    .get_keys = get_keys,
    .get_property = get_property,
    .set_property = set_property_on_immutable,
    .add = stub_add,
    .sub = stub_sub,
    .get_boolean_value = stub_get_boolean_value,
    .get_integer_value = stub_get_integer_value,
    .get_real_value = stub_get_real_value,
    .call = stub_call
};

/**
 * @var integer_proto
 * @brief The integer prototype object.
 * 
 * This is the integer prototype object, which is the instance that serves as the 
 * prototype for all integer objects.
 */
static object_t integer_proto = {
    .vtbl = &integer_proto_vtbl
};

object_t *get_integer_proto() {
    return &integer_proto;
}

/**
 * @brief Releases or clears a dynamic integer object.
 * 
 * This function either frees the object or resets its state and moves it to a list of reusable
 * objects, depending on the number of objects in the pool.
 * 
 * @param diobj The dynamic integer object to release or clear.
 */
static void release_or_clear(object_dynamic_integer_t *diobj) {
    remove_object_from_list(&diobj->base.process->objects, &diobj->base);
    if (diobj->base.process->integers.size == POOL_CAPACITY) {
        FREE(diobj);
    } else {
        diobj->refs = 0;
        diobj->state = ZOMBIE;
        diobj->value = 0;
        add_object_to_list(&diobj->base.process->integers, &diobj->base);
    }
}

/**
 * @brief Increments the reference count of an object.
 * @param obj The object whose reference count is to be incremented.
 */
static void inc_ref(object_t *obj) {
    object_dynamic_integer_t *diobj = (object_dynamic_integer_t *)obj;
    assert(diobj->state != ZOMBIE);
    diobj->refs++;
}

/**
 * @brief Decrements the reference count of an object.
 * @param obj The object whose reference count is to be decremented.
 */
static void dec_ref(object_t *obj) {
    object_dynamic_integer_t *diobj = (object_dynamic_integer_t *)obj;
    assert(diobj->state != ZOMBIE);
    if (!(--diobj->refs)) {
        release_or_clear(diobj);
    }
}

/**
 * @brief Marks an object as reachable during garbage collection.
 * @param obj The object to mark as reachable.
 */
static void mark(object_t *obj) {
    object_dynamic_integer_t *diobj = (object_dynamic_integer_t *)obj;
    assert(diobj->state != ZOMBIE);
    diobj->state = MARKED;
}

/**
 * @brief Sweeps the object, cleaning it up or moving it to the object pool.
 * @param obj The object to sweep.
 */
static void sweep(object_t *obj) {
    object_dynamic_integer_t *diobj = (object_dynamic_integer_t *)obj;
    assert(diobj->state != ZOMBIE);
    if (diobj->state == UNMARKED) {
        release_or_clear(diobj);
    } else {
        diobj->state = UNMARKED;
    }
}

/**
 * @brief Releases an integer object.
 * @param obj The object to release.
 */
static void release(object_t *obj) {
    object_dynamic_integer_t *diobj = (object_dynamic_integer_t *)obj;
    remove_object_from_list(
        diobj->state == ZOMBIE ? &obj->process->integers : &obj->process->objects, obj
    );
    FREE(obj);
}

/**
 * @brief Compares integer object and other numeric object based on their values.
 * @param obj1 The first object to compare.
 * @param obj2 The second object to compare.
 * @return An integer indicating the relative order: positive if obj1 > obj2,
 *  negative if obj1 < obj2, 0 if equal.
 */
static int compare(const object_t *obj1, const object_t *obj2) {
    double diff = obj1->vtbl->get_integer_value(obj1).value 
        - obj2->vtbl->get_real_value(obj2).value;
    if (diff > 0) {
        return 1;
    } else if (diff < 0) {
        return -1;
    } else {
        return 0;
    }
}

/**
 * @brief Clones an integer object.
 * @param process The process that will own the cloned object.
 * @param obj The integer object to be cloned.
 * @return A pointer to the cloned integer object. If the process is the same, the original object
 *  is returned; otherwise, a new object is created.
 */
static object_t *clone(process_t *process, object_t *obj) {
    if (process == obj->process) {
        return obj;
    }
    return create_integer_object(process, obj->vtbl->get_integer_value(obj).value);
}

/**
 * @brief Converts an integer object to a string representation.
 * @param obj The object to convert to a string.
 * @return A `string_value_t` structure containing the string representation of the object.
 *  The string is dynamically allocated and the caller must free it using `FREE`.
 */
static string_value_t to_string(const object_t *obj) {
    int64_t value = obj->vtbl->get_integer_value(obj).value;
    size_t buf_size = 24; // max 20 digits + sign + null terminator
    wchar_t *wstr = (wchar_t *)ALLOC(buf_size * sizeof(wchar_t));
    swprintf(wstr, buf_size, L"%" PRId64, value);
    return (string_value_t){ wstr, wcslen(wstr), true };
}

/**
 * @brief Converts an integer object to a Goat notation string representation.
 * @param obj The object to convert to a string in Goat notation.
 * @return A `string_value_t` structure containing the Goat notation string representation.
 *  The string is dynamically allocated and the caller must free it using `FREE`.
 */
static string_value_t to_string_notation(const object_t *obj) {
    return to_string(obj);
}

/**
 * @var prototypes
 * @brief Array of prototypes for the integer object.
 * 
 * It contains only the `integer_proto` prototype.
 */
static object_t* prototypes[] = {
    &integer_proto
};

/**
 * @brief Retrieves the prototypes of an integer object.
 * 
 * This function returns an array of prototypes for an integer object.
 * In this case, it contains only the integer prototype.
 * 
 * @param obj The object whose prototypes are to be retrieved.
 * @return An object_array_t containing the prototypes of the integer object.
 */
static object_array_t get_prototypes(const object_t *obj) {
    object_array_t result = {
        .items = prototypes,
        .size = 1
    };
    return result;
}

/**
 * @brief Retrieves the full prototype topology of an integer object.
 * 
 * This function returns the full prototype chain (topology) of an integer object.
 * The topology includes the `integer_proto` prototype, numeric prototype and the root object.
 * 
 * @param obj The object whose prototype topology is to be retrieved.
 * @return An object_array_t containing the full prototype chain.
 */
static object_array_t get_topology(const object_t *obj) {
    static object_t* topology[3] = {0};
    if (topology[0] == NULL) {
        topology[0] = &integer_proto;
        topology[1] = get_numeric_proto();
        topology[2] = get_root_object();
    }
    object_array_t result = {
        .items = topology,
        .size = 3
    };
    return result;
}

/**
 * @brief Adds two objects and returns the result as a new object.
 * @param process Process that will own the resulting object.
 * @param obj1 The first object to add.
 * @param obj2 The second object to add.
 * @return A pointer to the resulting object of the addition, or `NULL` if the second object 
 *  cannot be interpreted as an integer.
 */
static object_t *add(process_t *process, object_t *obj1, object_t *obj2) {
    int_value_t first = obj1->vtbl->get_integer_value(obj1);
    int_value_t second = obj2->vtbl->get_integer_value(obj2);
    if (!second.has_value) {
        return NULL;
    }
    return create_integer_object(process, first.value + second.value);
}

/**
 * @brief Subtracts the value of the second object from the first object.
 * @param process Process that will own the resulting object.
 * @param obj1 The first object (minuend).
 * @param obj2 The second object (subtrahend).
 * @return A pointer to the resulting object of the subtraction, or `NULL` if the second object 
 *  cannot be interpreted as an integer.
 */
static object_t *sub(process_t *process, object_t *obj1, object_t *obj2) {
    int_value_t first = obj1->vtbl->get_integer_value(obj1);
    int_value_t second = obj2->vtbl->get_integer_value(obj2);
    if (!second.has_value) {
        return NULL;
    }
    return create_integer_object(process, first.value - second.value);
}

/**
 * @brief Retrieves the boolean representation of an object.
 * @param obj The object from which to retrieve the boolean value.
 * @return Boolean representation of the object.
 */
static bool get_boolean_value(const object_t *obj) {
    return obj->vtbl->get_integer_value(obj).value != 0;
}

/**
 * @brief Retrieves the integer value of a static object.
 * @param obj The object from which to retrieve the integer value.
 * @return An `int_value_t` structure containing the integer value.
 */
static int_value_t static_get_integer_value(const object_t *obj) {
    object_static_integer_t *siobj = (object_static_integer_t *)obj;
    return (int_value_t){ true, siobj->value };
}

/**
 * @brief Retrieves the integer value of a dynamic object.
 * @param obj The object from which to retrieve the integer value.
 * @return An `int_value_t` structure containing the integer value.
 */
static int_value_t dynamic_get_integer_value(const object_t *obj) {
    object_dynamic_integer_t *diobj = (object_dynamic_integer_t *)obj;
    return (int_value_t){ true, diobj->value };
}

/**
 * @brief Retrieves value of a static object casted to real.
 * @param obj The object from which to retrieve the real value.
 * @return A `real_value_t` structure containing the real value.
 */
static real_value_t static_get_real_value(const object_t *obj) {
    object_static_integer_t *siobj = (object_static_integer_t *)obj;
    return (real_value_t){ true, (double)siobj->value };
}

/**
 * @brief Retrieves value of a dynamic object casted to real.
 * @param obj The object from which to retrieve the real value.
 * @return A `real_value_t` structure containing the real value.
 */
static real_value_t dynamic_get_real_value(const object_t *obj) {
    object_dynamic_integer_t *diobj = (object_dynamic_integer_t *)obj;
    return (real_value_t){ true, (double)diobj->value };
}

/**
 * @var vtbl
 * @brief This virtual table defines the behavior of the static integer object.
 */
static object_vtbl_t static_vtbl = {
    .type = TYPE_NUMBER,
    .inc_ref = stub_memory_function,
    .dec_ref = stub_memory_function,
    .mark = stub_memory_function,
    .sweep = stub_memory_function,
    .release = stub_memory_function,
    .compare = compare,
    .clone = clone,
    .to_string = to_string,
    .to_string_notation = to_string_notation,
    .get_prototypes = get_prototypes,
    .get_topology = get_topology,
    .get_keys = get_keys,
    .get_property = get_property,
    .set_property = set_property_on_immutable,
    .add = add,
    .sub = sub,
    .get_boolean_value = get_boolean_value,
    .get_integer_value = static_get_integer_value,
    .get_real_value = static_get_real_value,
    .call = stub_call
};

/**
 * @brief The total number of static integer objects.
 */
#define STATIC_INTEGER_RANGE (MAX_STATIC_INTEGER - MIN_STATIC_INTEGER + 1)

/**
 * @brief Static array of objects representing static integer values.
 * 
 * This array holds objects corresponding to integers in the range defined by
 * `MIN_STATIC_INTEGER` to `MAX_STATIC_INTEGER`.
 */
static object_static_integer_t static_integers[STATIC_INTEGER_RANGE];

/**
 * @brief Flag to indicate whether the static integers array has been initialized.
 */
static bool is_static_integers_initialized = false;

/**
 * @brief Initializes the static integer objects array.
 * 
 * This function initializes the static array of integer objects for the range
 * `MIN_STATIC_INTEGER` to `MAX_STATIC_INTEGER`. It is automatically invoked during the
 * first call to `get_static_integer_object()`.
 */
static void initialize_static_integers() {
    for (int i = MIN_STATIC_INTEGER; i <= MAX_STATIC_INTEGER; ++i) {
        static_integers[i - MIN_STATIC_INTEGER] = (object_static_integer_t){
            { &static_vtbl, NULL, NULL, NULL },
            i
        };
    }
    is_static_integers_initialized = true;
}

object_t *get_static_integer_object(int value) {
    assert(value >= MIN_STATIC_INTEGER && value <= MAX_STATIC_INTEGER);
    if (!is_static_integers_initialized) {
        initialize_static_integers();
    }
    return &static_integers[value - MIN_STATIC_INTEGER].base;
}

object_t *get_integer_zero() {
    return get_static_integer_object(0);
}

/**
 * @var vtbl
 * @brief This virtual table defines the behavior of the dynamic integer object.
 */
static object_vtbl_t dynamic_vtbl = {
    .type = TYPE_NUMBER,
    .inc_ref = inc_ref,
    .dec_ref = dec_ref,
    .mark = mark,
    .sweep = sweep,
    .release = release,
    .compare = compare,
    .clone = clone,
    .to_string = to_string,
    .to_string_notation = to_string_notation,
    .get_prototypes = get_prototypes,
    .get_topology = get_topology,
    .get_keys = get_keys,
    .get_property = get_property,
    .set_property = set_property_on_immutable,
    .add = add,
    .sub = sub,
    .get_boolean_value = get_boolean_value,
    .get_integer_value = dynamic_get_integer_value,
    .get_real_value = dynamic_get_real_value,
    .call = stub_call
};

object_t *create_integer_object(process_t *process, int64_t value) {
    if (value >= MIN_STATIC_INTEGER && value <= MAX_STATIC_INTEGER) {
        return get_static_integer_object((int)value);
    }
    object_dynamic_integer_t *obj;
    if (process->integers.size > 0) {
        obj = (object_dynamic_integer_t *)remove_first_object_from_list(&process->integers);
    } else {
        obj = (object_dynamic_integer_t *)CALLOC(sizeof(object_dynamic_integer_t));
        obj->base.vtbl = &dynamic_vtbl;
        obj->base.process = process;
    }
    obj->refs = 1;
    obj->state = UNMARKED;
    obj->value = value;
    add_object_to_list(&process->objects, &obj->base);
    return &obj->base;
}
