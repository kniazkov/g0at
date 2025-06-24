/**
 * @file real.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of an object representing a real number.
 *
 * This file defines the structure and behavior of real number objects. There are two types
 * of real number objects:
 * 1. Static real numbers:
 *    - These include mathematical constants declared in the model (like Pi or Euler's number).
 * 2. Dynamic real numbers:
 *    - These are created as a result of operations at runtime.
 *    - They internally store their own value and are subject to garbage collection when
 *      no longer in use.
 */
#include <assert.h>
#include <stdio.h>
#include <math.h>

#include "object.h"
#include "object_state.h"
#include "process.h"
#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

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
 * @struct object_static_real_t
 * @brief Structure representing a static real number object.
 * 
 * Static real numbers are used for mathematical constants and other immutable
 * floating-point values that persist throughout the program execution.
 * They are not managed by the garbage collector.
 */
typedef struct {
    object_t base; ///< The base object that provides common functionality.
    double value; ///< The double-precision floating-point value of the object.
} object_static_real_t;

/**
 * @struct object_dynamic_real_t
 * @brief Structure representing a dynamic real number object.
 * 
 * Dynamic real numbers are created during program execution for temporary values,
 * calculation results, and other runtime floating-point operations.
 * They are managed by the garbage collector using reference counting.
 */
typedef struct {
    object_t base; ///< The base object that provides common functionality.
    int refs; ///< Reference count for garbage collection.
    object_state_t state; ///< The state of the object (unmarked, marked, or zombie).
    double value; ///< The double-precision floating-point value of the object.
} object_dynamic_real_t;

/**
 * @brief Retrieves the prototypes of the real number prototype object.
 * 
 * This function returns the prototype chain for real number objects. Real numbers
 * inherit from the numeric prototype, which provides common mathematical
 * operations and properties.
 * 
 * @param obj The real number object whose prototypes are to be retrieved.
 * @return An object_array_t containing the numeric prototype (singleton array).
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
 * @brief Retrieves the full prototype topology of the real number prototype object.
 * 
 * This function returns the full prototype chain (topology) of the real number prototype object. 
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
 * @var real_proto_vtbl
 * @brief Virtual table defining the behavior of the real number prototype object.
 */
static object_vtbl_t real_proto_vtbl = {
    .type = TYPE_OTHER,
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
 * @var real_proto
 * @brief The real number prototype object.
 * 
 * This is the real number prototype object, which is the instance that serves as the 
 * prototype for all real number objects.
 */
static object_t real_proto = {
    .vtbl = &real_proto_vtbl
};

object_t *get_real_proto() {
    return &real_proto;
}

/**
 * @brief Releases or clears a dynamic real number object.
 * 
 * This function either frees the object or resets its state and moves it to a list of reusable
 * objects, depending on the number of objects in the pool.
 * 
 * @param diobj The dynamic real number object to release or clear.
 */
static void release_or_clear(object_dynamic_real_t *drobj) {
    remove_object_from_list(&drobj->base.process->objects, &drobj->base);
    if (drobj->base.process->integers.size == POOL_CAPACITY) {
        FREE(drobj);
    } else {
        drobj->refs = 0;
        drobj->state = ZOMBIE;
        drobj->value = 0;
        add_object_to_list(&drobj->base.process->real_numbers, &drobj->base);
    }
}

/**
 * @brief Increments the reference count of an object.
 * @param obj The object whose reference count is to be incremented.
 */
static void inc_ref(object_t *obj) {
    object_dynamic_real_t *drobj = (object_dynamic_real_t *)obj;
    assert(drobj->state != ZOMBIE);
    drobj->refs++;
}

/**
 * @brief Decrements the reference count of an object.
 * @param obj The object whose reference count is to be decremented.
 */
static void dec_ref(object_t *obj) {
    object_dynamic_real_t *drobj = (object_dynamic_real_t *)obj;
    assert(drobj->state != ZOMBIE);
    if (!(--drobj->refs)) {
        release_or_clear(drobj);
    }
}

/**
 * @brief Marks an object as reachable during garbage collection.
 * @param obj The object to mark as reachable.
 */
static void mark(object_t *obj) {
    object_dynamic_real_t *drobj = (object_dynamic_real_t *)obj;
    assert(drobj->state != ZOMBIE);
    drobj->state = MARKED;
}

/**
 * @brief Sweeps the object, cleaning it up or moving it to the object pool.
 * @param obj The object to sweep.
 */
static void sweep(object_t *obj) {
    object_dynamic_real_t *drobj = (object_dynamic_real_t *)obj;
    assert(drobj->state != ZOMBIE);
    if (drobj->state == UNMARKED) {
        release_or_clear(drobj);
    } else {
        drobj->state = UNMARKED;
    }
}

/**
 * @brief Releases a real number object.
 * @param obj The object to release.
 */
static void release(object_t *obj) {
    object_dynamic_real_t *diobj = (object_dynamic_real_t *)obj;
    remove_object_from_list(
        diobj->state == ZOMBIE ? &obj->process->real_numbers : &obj->process->objects, obj
    );
    FREE(obj);
}

/**
 * @brief Compares real number object and other numeric object based on their values.
 * @param obj1 The first object to compare.
 * @param obj2 The second object to compare.
 * @return An integer indicating the relative order: positive if obj1 > obj2,
 *  negative if obj1 < obj2, 0 if equal.
 */
static int compare(const object_t *obj1, const object_t *obj2) {
    double diff = obj1->vtbl->get_real_value(obj1).value 
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
 * @brief Clones a real number object.
 * @param process The process that will own the cloned object.
 * @param obj The real number object to be cloned.
 * @return A pointer to the cloned real number object. If the process is the same, the original
 *  object is returned; otherwise, a new object is created.
 */
static object_t *clone(process_t *process, object_t *obj) {
    if (process == obj->process) {
        return obj;
    }
    return create_real_number_object(process, obj->vtbl->get_real_value(obj).value);
}

/**
 * @brief Converts a real number object to a string representation.
 * 
 * This function converts a real number object to its string representation with the following
 * characteristics:
 * - Uses standard decimal notation for most values
 * - Automatically switches to scientific notation for very large/small numbers
 * - Preserves full precision of the double value
 * - Formats according to the following rules:
 *   - Omits decimal point for integer values (e.g., "5.0" → "5")
 *   - Trims trailing zeros after decimal point (e.g., "3.14000" → "3.14")
 *   - Always shows at least one digit before decimal point
 * 
 * @param obj The real number object to convert to a string.
 * @return A `string_value_t` structure containing the formatted string, its length, and a flag 
 *         indicating ownership of the buffer.
 */
static string_value_t to_string(const object_t *obj) {
    double value = obj->vtbl->get_real_value(obj).value;
    return format_string(L"%f", value);
}

/**
 * @brief Converts a real number object to a Goat notation string representation.
 * @param obj The object to convert to a string in Goat notation.
 * @return A `string_value_t` structure containing the Goat notation real number representation.
 *  The string is dynamically allocated and the caller must free it using `FREE`.
 */
static string_value_t to_string_notation(const object_t *obj) {
    return to_string(obj);
}

/**
 * @var prototypes
 * @brief Array of prototypes for the real number object.
 * 
 * It contains only the `real_proto` prototype.
 */
static object_t* prototypes[] = {
    &real_proto
};

/**
 * @brief Retrieves the prototypes of a real number object.
 * 
 * This function returns an array of prototypes for a real number object.
 * In this case, it contains only the real number prototype.
 * 
 * @param obj The object whose prototypes are to be retrieved.
 * @return An object_array_t containing the prototypes of the real number object.
 */
static object_array_t get_prototypes(const object_t *obj) {
    object_array_t result = {
        .items = prototypes,
        .size = 1
    };
    return result;
}

/**
 * @brief Retrieves the full prototype topology of a real number object.
 * 
 * This function returns the full prototype chain (topology) of a real number object.
 * The topology includes the `real_proto` prototype, numeric prototype and the root object.
 * 
 * @param obj The object whose prototype topology is to be retrieved.
 * @return An object_array_t containing the full prototype chain.
 */
static object_array_t get_topology(const object_t *obj) {
    static object_t* topology[3] = {0};
    if (topology[0] == NULL) {
        topology[0] = &real_proto;
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
 *  cannot be interpreted as a real number.
 */
static object_t *add(process_t *process, object_t *obj1, object_t *obj2) {
    real_value_t first = obj1->vtbl->get_real_value(obj1);
    real_value_t second = obj2->vtbl->get_real_value(obj2);
    if (!second.has_value) {
        return NULL;
    }
    return create_real_number_object(process, first.value + second.value);
}

/**
 * @brief Subtracts the value of the second object from the first object.
 * @param process Process that will own the resulting object.
 * @param obj1 The first object (minuend).
 * @param obj2 The second object (subtrahend).
 * @return A pointer to the resulting object of the subtraction, or `NULL` if the second object 
 *  cannot be interpreted as a real number.
 */
static object_t *sub(process_t *process, object_t *obj1, object_t *obj2) {
    real_value_t first = obj1->vtbl->get_real_value(obj1);
    real_value_t second = obj2->vtbl->get_real_value(obj2);
    if (!second.has_value) {
        return NULL;
    }
    return create_real_number_object(process, first.value - second.value);
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
 * @brief Attempts to retrieve an integer value from a static real number object.
 * 
 * Static real numbers represent mathematical constants (like Pi) that are inherently
 * non-integer values. This function always indicates failure since static reals
 * cannot represent exact integer values by design.
 * 
 * @param obj The static real number object to check.
 * @return An `int_value_t` structure with has_value=false, as static reals cannot
 *         be represented as exact integers.
 */
static int_value_t static_get_integer_value(const object_t *obj) {
    return (int_value_t){ false, 0 };
}

/**
 * @brief Attempts to retrieve an integer value from a dynamic real number object.
 * 
 * For dynamic real numbers, this function checks if the stored value:
 * 1. Has no fractional part (e.g., 5.0, -3.0)
 * 2. Falls within the range of int64_t
 * 
 * If both conditions are met, returns the integer value with has_value=true.
 * Otherwise returns has_value=false indicating the value cannot be represented
 * as an exact integer.
 * 
 * @param obj The dynamic real number object to convert.
 * @return An `int_value_t` structure containing either:
 *         - The converted integer when exact conversion is possible
 *         - has_value=false when the value is fractional or out of range
 */
static int_value_t dynamic_get_integer_value(const object_t *obj) {
    object_dynamic_real_t *drobj = (object_dynamic_real_t *)obj;
    double value = drobj->value;
    if (value == trunc(value)) {
        if (value >= (double)INT64_MIN && value <= (double)INT64_MAX) {
            return (int_value_t){ true, (int64_t)value };
        }
    }
    return (int_value_t){ false, 0 };
}

/**
 * @brief Retrieves value of a static real number object.
 * @param obj The object from which to retrieve the real value.
 * @return A `real_value_t` structure containing the real value.
 */
static real_value_t static_get_real_value(const object_t *obj) {
    object_static_real_t *srobj = (object_static_real_t *)obj;
    return (real_value_t){ true, srobj->value };
}

/**
 * @brief Retrieves value of a dynamic real number object.
 * @param obj The object from which to retrieve the real value.
 * @return A `real_value_t` structure containing the real value.
 */
static real_value_t dynamic_get_real_value(const object_t *obj) {
    object_dynamic_real_t *drobj = (object_dynamic_real_t *)obj;
    return (real_value_t){ true, drobj->value };
}

/**
 * @var static_vtbl
 * @brief This virtual table defines the behavior of the static real number object.
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
 * @brief Static real number object representing the mathematical constant π (Pi).
 */
static object_static_real_t pi_object = {
    .base = {
        .vtbl = &static_vtbl
    },
    .value = M_PI
};

object_t* get_pi_object() {
    return &pi_object.base;
}

/**
 * @var dynamic_vtbl
 * @brief This virtual table defines the behavior of the dynamic real number object.
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

object_t *create_real_number_object(process_t *process, double value) {
    object_dynamic_real_t *obj;
    if (process->real_numbers.size > 0) {
        obj = (object_dynamic_real_t *)remove_first_object_from_list(&process->real_numbers);
    } else {
        obj = (object_dynamic_real_t *)CALLOC(sizeof(object_dynamic_real_t));
        obj->base.vtbl = &dynamic_vtbl;
        obj->base.process = process;
    }
    obj->refs = 1;
    obj->state = UNMARKED;
    obj->value = value;
    add_object_to_list(&process->objects, &obj->base);
    return &obj->base;
}
