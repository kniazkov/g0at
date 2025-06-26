/**
 * @file string.с
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementations of an object representing a string.
 *
 * This file defines the structure and behavior of string objects. There are two types
 * of string objects:
 * 1. Static strings:
 *    - These include strings declared in the model, such as identifiers of built-in functions
 *      (e.g., `print`).
 *    - They can also be strings loaded from bytecode.
 *    - Static strings only store a reference to their data, which exists for the duration of the
 *      program's execution. These strings are not managed by the garbage collector.
 * 2. Dynamic strings:
 *    - These are created as a result of string operations at runtime.
 *    - They internally store their own data array and are subject to garbage collection
 *      when no longer in use.
 */

#include <assert.h>

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
 * @struct object_static_string_t
 * @brief Structure representing a static string object.
 * 
 * Static strings are immutable and exist for the entire duration of the program's execution.
 * They only hold a reference to their data, which is not managed by the garbage collector.
 */
typedef struct {
    object_t base; ///< The base object that provides common functionality.
    wchar_t *data; ///< Pointer to the string data (wide character array).
    size_t length; ///< Length of the string (number of characters).
} object_static_string_t;

/**
 * @struct object_dynamic_string_t
 * @brief Structure representing a dynamic string object.
 * 
 * Dynamic strings are also immutable but are created at runtime, typically as a result of
 * string operations. These strings manage their own data and are subject to garbage collection
 * when no longer used.
 */
typedef struct {
    object_t base; ///< The base object that provides common functionality.
    int refs; ///< Reference count used for garbage collection.
    object_state_t state; ///< The state of the object (e.g., unmarked, marked, or zombie).
    wchar_t *data; ///< Pointer to the string data (wide character array).
    size_t length; ///< Length of the string (number of characters).
} object_dynamic_string_t;

/**
 * @brief Retrieves all property keys from a string object.
 * 
 * This function returns a static array containing the property keys associated with a string
 * object. The array is initialized lazily during the first invocation of the function.
 * 
 * @param obj The object from which to retrieve the keys.
 * @return An object array containing all property keys.
 */
static object_array_t get_keys(const object_t *obj) {
    static object_t *keys[1] = { NULL };
    if (keys[0] == NULL) {
        keys[0] = get_string_length();
    }
    return (object_array_t){ keys, sizeof(keys) / sizeof(object_t*) };
}

/**
 * @brief Retrieves a property value from the prototype of a string object.
 * 
 * This function checks if the given `key` corresponds to a known property of the string object 
 * prototype. Currently, it only supports the `length` property, which returns a static integer 
 * object representing zero. If the key is not recognized or does not match the `length` property,
 * the function returns `NULL`.
 * 
 * @param obj The string object whose prototype property is being retrieved.
 * @param key The key of the property to retrieve.
 * @return A pointer to the value of the property, or `NULL` if the key is not recognized.
 */
static object_t *proto_get_property(const object_t *obj, const object_t *key) {
    object_t *value = NULL;
    if (key->vtbl->type == TYPE_STRING) {
        string_value_t key_str = key->vtbl->to_string(key);
        if (wcscmp(L"length", key_str.data) == 0) {
            value = get_integer_zero();
        }
    }
    return value;
}

/**
 * @var string_proto_vtbl
 * @brief Virtual table defining the behavior of the prototype string object.
 */
static object_vtbl_t string_proto_vtbl = {
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
    .get_prototypes = common_get_prototypes,
    .get_topology = common_get_topology,
    .get_keys = get_keys,
    .get_property = proto_get_property,
    .add_property = add_property_on_immutable,
    .set_property = set_property_on_immutable,
    .add = stub_add,
    .sub = stub_sub,
    .get_boolean_value = stub_get_boolean_value,
    .get_integer_value = stub_get_integer_value,
    .get_real_value = stub_get_real_value,
    .call = stub_call
};

/**
 * @var string_proto
 * @brief The prototype string object.
 * 
 * This is the prototype string object, which is the instance that serves as the 
 * prototype for all string objects.
 */
static object_t string_proto = {
    .vtbl = &string_proto_vtbl
};

object_t *get_string_proto() {
    return &string_proto;
}

/**
 * @brief Releases or clears a dynamic string object.
 * 
 * This function manages the lifecycle of a dynamic string object. If the pool of reusable 
 * objects (`dynamic_strings`) in the process has reached its capacity, the object is fully 
 * deallocated. Otherwise, the object is reset (its data memory is freed, and its internal 
 * fields are cleared) and added to the pool for reuse, reducing the overhead of allocating 
 * new objects.
 * 
 * @param dsobj The dynamic string object to release or clear.
 */
static void release_or_clear(object_dynamic_string_t *dsobj) {
    FREE(dsobj->data);
    remove_object_from_list(&dsobj->base.process->objects, &dsobj->base);
    if (dsobj->base.process->dynamic_strings.size == POOL_CAPACITY) {
        FREE(dsobj);
    } else {
        dsobj->refs = 0;
        dsobj->state = ZOMBIE;
        dsobj->data = NULL;
        dsobj->length = 0;
        add_object_to_list(&dsobj->base.process->dynamic_strings, &dsobj->base);
    }
}

/**
 * @brief Increments the reference count of an object.
 * @param obj The object whose reference count is to be incremented.
 */
static void inc_ref(object_t *obj) {
    object_dynamic_string_t *dsobj = (object_dynamic_string_t *)obj;
    assert(dsobj->state != ZOMBIE);
    dsobj->refs++;
}

/**
 * @brief Decrements the reference count of an object.
 * @param obj The object whose reference count is to be decremented.
 */
static void dec_ref(object_t *obj) {
    object_dynamic_string_t *dsobj = (object_dynamic_string_t *)obj;
    assert(dsobj->state != ZOMBIE);
    if (!(--dsobj->refs)) {
        release_or_clear(dsobj);
    }
}

/**
 * @brief Marks an object as reachable during garbage collection.
 * @param obj The object to mark as reachable.
 */
static void mark(object_t *obj) {
    object_dynamic_string_t *dsobj = (object_dynamic_string_t *)obj;
    assert(dsobj->state != ZOMBIE);
    dsobj->state = MARKED;
}

/**
 * @brief Sweeps the object, cleaning it up or moving it to the object pool.
 * @param obj The object to sweep.
 */
static void sweep(object_t *obj) {
    object_dynamic_string_t *dsobj = (object_dynamic_string_t *)obj;
    assert(dsobj->state != ZOMBIE);
    if (dsobj->state == UNMARKED) {
        release_or_clear(dsobj);
    } else {
        dsobj->state = UNMARKED;
    }
}

/**
 * @brief Releases a dynamic string object.
 * @param obj The object to release.
 */
static void release(object_t *obj) {
    object_dynamic_string_t *dsobj = (object_dynamic_string_t *)obj;
    remove_object_from_list(
        dsobj->state == ZOMBIE ? &obj->process->dynamic_strings : &obj->process->objects, obj
    );
    FREE(dsobj->data);
    FREE(obj);
}

/**
 * @brief Compares two string objects based on their values.
 * @param obj1 The first object to compare.
 * @param obj2 The second object to compare.
 * @return An integer indicating the relative order: positive if obj1 > obj2,
 *  negative if obj1 < obj2, 0 if equal.
 */
static int compare(const object_t *obj1, const object_t *obj2) {
    string_value_t first = obj1->vtbl->to_string(obj1);
    string_value_t second = obj2->vtbl->to_string(obj2);
    int result = wcscmp(first.data, second.data);
    if (second.should_free) {
        FREE(second.data);
    }
    return result;
}

/**
 * @brief Clones a string object.
 * @param process The process that will own the cloned object.
 * @param obj The string object to be cloned.
 * @return A pointer to the cloned string object. If the process is the same, the original object
 *  is returned; otherwise, a new object is created.
 */
static object_t *clone(process_t *process, object_t *obj) {
    if (process == obj->process) {
        return obj;
    }
    string_value_t value = obj->vtbl->to_string(obj);
    return create_string_object(process, value);
}

/**
 * @brief Returns the string data of the static string object.
 * @param obj The static string object to convert to a string.
 * @return A `string_value_t` structure containing the string data and a `false` flag
 *  (indicating the string is not dynamically allocated).
 */
static string_value_t static_to_string(const object_t *obj) {
    object_static_string_t *stsobj = (object_static_string_t *)obj;
    return (string_value_t){ stsobj->data, stsobj->length, false };
}

/**
 * @brief Returns the string data of the dynamic string object.
 * @param obj The dynamic string object to convert to a string.
 * @return A `string_value_t` structure containing the string data and a `false` flag
 *  (indicating the string is not dynamically allocated).
 */
static string_value_t dynamic_to_string(const object_t *obj) {
    object_dynamic_string_t *dsobj = (object_dynamic_string_t *)obj;
    return (string_value_t){ dsobj->data, dsobj->length, false };
}

/**
 * @brief Converts a string object to a Goat notation string representation.
 * 
 * This function converts the object into a string formatted in Goat notation,
 * escaping special characters like newline, carriage return, tab, quote, and backslash.
 * The resulting string is dynamically allocated and the caller must free it using `FREE`.
 * 
 * @param obj The object to convert to a string in Goat notation.
 * @return A `string_value_t` structure containing the Goat notation string representation.
 *         The string is dynamically allocated, and the caller must free it using `FREE`.
 */
static string_value_t to_string_notation(const object_t *obj) {
    string_value_t value = obj->vtbl->to_string(obj);
    return string_to_string_notation(L"", value);
}

/**
 * @var prototypes
 * @brief Array of prototypes for the string object.
 * 
 * It contains only the `string_proto` prototype.
 */
static object_t* prototypes[] = {
    &string_proto
};

/**
 * @brief Retrieves the prototypes of a string object.
 * 
 * This function returns an array of prototypes for a string object.
 * In this case, it contains only the string prototype.
 * 
 * @param obj The object whose prototypes are to be retrieved.
 * @return An object_array_t containing the prototypes of the string object.
 */
static object_array_t get_prototypes(const object_t *obj) {
    object_array_t result = {
        .items = prototypes,
        .size = 1
    };
    return result;
}

/**
 * @brief Retrieves the full prototype topology of a string object.
 * 
 * This function returns the full prototype chain (topology) of a string object.
 * The topology includes the `string_proto` prototype and the root object.
 * 
 * @param obj The object whose prototype topology is to be retrieved.
 * @return An object_array_t containing the full prototype chain.
 */
static object_array_t get_topology(const object_t *obj) {
    static object_t* topology[2] = {0};
    if (topology[0] == NULL) {
        topology[0] = &string_proto;
        topology[1] = get_root_object();
    }
    object_array_t result = {
        .items = topology,
        .size = 2
    };
    return result;
}

/**
 * @brief Retrieves the value of a property from a static string object.
 * @param obj The static string object whose property is being accessed.
 * @param key The key of the property to retrieve.
 * @return A pointer to the value of the property, or `NULL` if the key is not recognized.
 * @note A static string has only static properties.
 */
static object_t *static_get_property(const object_t *obj, const object_t *key) {
    if (key->vtbl->type == TYPE_STRING) {
        object_static_string_t *stsobj = (object_static_string_t *)obj;
        string_value_t key_str = key->vtbl->to_string(key);
        if (wcscmp(L"length", key_str.data) == 0) {
            return get_static_integer_object((int)stsobj->length);
        }
    }
    return NULL;
}

/**
 * @brief Retrieves the value of a property from a dynamic string object.
 * @param obj The static string object whose property is being accessed.
 * @param key The key of the property to retrieve.
 * @return A pointer to the value of the property, or `NULL` if the key is not recognized.
 */
static object_t *dynamic_get_property(const object_t *obj, const object_t *key) {
    if (key->vtbl->type == TYPE_STRING) {
        object_dynamic_string_t *dsobj = (object_dynamic_string_t *)obj;
        string_value_t key_str = key->vtbl->to_string(key);
        if (wcscmp(L"length", key_str.data) == 0) {
            return create_integer_object(obj->process, (int64_t)dsobj->length);
        }
    }
    return NULL;
}

/**
 * @brief Adds two objects together, concatenating their string representations.
 * 
 * This function converts the two objects to their string representations and concatenates them
 * into a new string object. If either of the objects is an empty string, the other object is
 * returned as the result. If both objects have non-empty string representations, their strings are
 * concatenated into a new dynamic string object.
 * 
 * @param process Process that will own the resulting object.
 * @param obj1 The first object to add (string, static or dynamic).
 * @param obj2 The second object to add (converted to string).
 * @return A pointer to the resulting object after concatenation.
 */
static object_t *add(process_t *process, object_t *obj1, object_t *obj2) {
    string_value_t first = obj1->vtbl->to_string(obj1);
    if (first.length == 0) {
        if (obj2->vtbl->type == TYPE_STRING) {
            INCREF(obj2);
            return obj2;
        }
        return create_string_object(process, obj2->vtbl->to_string(obj2));
    }
    string_value_t second = obj2->vtbl->to_string(obj2);
    if (second.length == 0) {
        INCREF(obj1);
        return obj1;
    }
    string_builder_t builder;
    init_string_builder(&builder, first.length + second.length);
    append_substring(&builder, first.data, first.length);
    string_value_t result = append_substring(&builder, second.data, second.length);
    if (second.should_free) {
        FREE(second.data);
    }
    return create_string_object(process, result);
}

/**
 * @brief Retrieves the boolean representation of a string object.
 * 
 * This function converts the string object to its string representation and returns `true` if the 
 * string is non-empty, and `false` otherwise. An empty string is considered as a falsy value.
 * 
 * @param obj The object from which to retrieve the boolean value.
 * @return `true` if the string is non-empty, `false` if the string is empty.
 */

static bool get_boolean_value(const object_t *obj) {
    string_value_t value = obj->vtbl->to_string(obj);
    return value.length > 0;
}

/**
 * @var static_string_vtbl
 * @brief Virtual table defining the behavior of the static string object.
 */
static object_vtbl_t static_string_vtbl = {
    .type = TYPE_STRING,
    .inc_ref = stub_memory_function,
    .dec_ref = stub_memory_function,
    .mark = stub_memory_function,
    .sweep = stub_memory_function,
    .release = stub_memory_function,
    .compare = compare,
    .clone = clone,
    .to_string = static_to_string,
    .to_string_notation = to_string_notation,
    .get_prototypes = get_prototypes,
    .get_topology = get_topology,
    .get_keys = get_keys,
    .get_property = static_get_property,
    .add_property = add_property_on_immutable,
    .set_property = set_property_on_immutable,
    .add = add,
    .sub = stub_sub,
    .get_boolean_value = get_boolean_value,
    .get_integer_value = stub_get_integer_value,
    .get_real_value = stub_get_real_value,
    .call = stub_call
};

/**
 * @brief Macro to declare a static string object and provide access to it.
 * 
 * This macro defines a static string object with a specified name and string value. It also
 * provides a function to retrieve the object. The string is stored as a constant literal, and
 * the object is initialized with the appropriate virtual table.
 * 
 * @param name The name of the static string object.
 * @param string The wide-character string literal.
 */
#define DECLARE_STATIC_STRING(name, string) \
    static object_static_string_t name = \
        { { &static_string_vtbl, NULL, NULL, NULL }, (string), sizeof(string) / sizeof(wchar_t) - 1 }; \
    object_t *get_##name() { return &name.base; } 

/**
 * @brief Declares some common static string objects.
 */
DECLARE_STATIC_STRING(empty_string, L"")
DECLARE_STATIC_STRING(string_length, L"length")
DECLARE_STATIC_STRING(string_pi, L"pi")
DECLARE_STATIC_STRING(string_print, L"print")
DECLARE_STATIC_STRING(string_sign, L"sign")

/**
 * @var dynamic_string_vtbl
 * @brief Virtual table defining the behavior of the dynamic string object.
 */
static object_vtbl_t dynamic_string_vtbl = {
    .type = TYPE_STRING,
    .inc_ref = inc_ref,
    .dec_ref = dec_ref,
    .mark = mark,
    .sweep = sweep,
    .release = release,
    .compare = compare,
    .clone = clone,
    .to_string = dynamic_to_string,
    .to_string_notation = to_string_notation,
    .get_prototypes = get_prototypes,
    .get_topology = get_topology,
    .get_keys = get_keys,
    .get_property = dynamic_get_property,
    .add_property = add_property_on_immutable,
    .set_property = set_property_on_immutable,
    .add = add,
    .sub = stub_sub,
    .get_boolean_value = get_boolean_value,
    .get_integer_value = stub_get_integer_value,
    .get_real_value = stub_get_real_value,
    .call = stub_call
};

object_t *create_string_object(process_t *process, string_value_t value) {
    if (value.length == 0) {
        if (value.should_free) {
            FREE(value.data);
        }
        return get_empty_string();
    }
    object_dynamic_string_t *obj;
    if (process->dynamic_strings.size > 0) {
        obj = (object_dynamic_string_t *)remove_first_object_from_list(&process->dynamic_strings);
    } else {
        obj = (object_dynamic_string_t *)CALLOC(sizeof(object_dynamic_string_t));
        obj->base.vtbl = &dynamic_string_vtbl;
        obj->base.process = process;
    }
    obj->refs = 1;
    obj->state = UNMARKED;
    obj->data = value.should_free ? value.data : WSTRDUP(value.data);
    obj->length = value.length;
    add_object_to_list(&process->objects, &obj->base);
    return &obj->base;
}
