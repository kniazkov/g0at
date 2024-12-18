/**
 * @file object_user_defined.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Definitions and methods for user-defined objects in the Goat programming language.
 * 
 * This file provides the structure and functionality for a user-defined object type in the
 * Goat programming language. A user-defined object is a general-purpose object that allows storing
 * arbitrary data and methods in the form of key-value pairs.
 */

#include <assert.h>

#include "object.h"
#include "object_state.h"
#include "process.h"
#include "lib/allocate.h"
#include "lib/avl_tree.h"
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
 * @struct object_user_defined_t
 * @brief Structure representing a user-defined object.
 */
typedef struct {
    /**
     * @brief The base object that provides common functionality.
     */
    object_t base;
    
    /**
     * @brief Reference count.
     */
    int refs;

    /**
     * @brief The state of the object (e.g., unmarked, marked, or zombie).
     */
    object_state_t state;

    /**
     * @brief AVL tree storing key-value pairs, where both keys and values are objects.
     */
    avl_tree_t *children;
} object_user_defined_t;

/**
 * @brief Creates an empty user-defined object.
 * 
 * This function initializes an empty user-defined object. If there are recycled objects 
 * available in the process's pool, one of them is reused. Otherwise, a new object is allocated. 
 * The object is added to the process's object list and initialized with default settings, 
 * including an empty AVL tree for storing key-value pairs.
 * 
 * @param process The process that will own the created object.
 * @return A pointer to the newly created or recycled user-defined object.
 */
static object_user_defined_t *create_empty_user_defined_object(process_t* process);

/**
 * @brief Decrements the reference count of a key-value pair in the user-defined object's children.
 * 
 * This function is called for each key-value pair in the AVL tree of a user-defined object. 
 * It decreases the reference count of both the key and the value, ensuring proper memory 
 * management and cleanup of referenced objects.
 * 
 * @param unused Unused parameter, included for compatibility with the AVL tree traversal function.
 * @param key The key of the key-value pair, cast to an object.
 * @param value The value of the key-value pair, stored as a `value_t` type.
 */
static void clear_child_pair(void *unused, void *key, value_t value) {
    DECREF((object_t *)key);
    DECREF((object_t *)value.ptr);
}

/**
 * @brief Clears a user-defined object.
 * 
 * This function iterates through all key-value pairs stored in the children AVL tree of 
 * a user-defined object, decrementing their reference counts and freeing associated memory. 
 * After clearing the contents, it empties the AVL tree but does not deallocate the tree structure.
 * 
 * @param uobj A pointer to the user-defined object to clear.
 */
static void clear(object_user_defined_t *uobj) {
    avl_tree_for_each(uobj->children, clear_child_pair, NULL);
    clear_avl_tree(uobj->children);
}

/**
 * @brief Releases or clears a user-defined object.
 * 
 * This function either frees the object or resets its state and moves it to a list of reusable
 * objects, depending on the number of objects in the pool.
 * 
 * @param iobj The user-defined object to release or clear.
 */
static void release_or_clear(object_user_defined_t *uobj) {
    clear(uobj);
    remove_object_from_list(&uobj->base.process->objects, &uobj->base);
    if (uobj->base.process->user_defined_objects.size == POOL_CAPACITY) {
        destroy_avl_tree(uobj->children);
        FREE(uobj);
    } else {
        uobj->refs = 0;
        uobj->state = ZOMBIE;
        add_object_to_list(&uobj->base.process->user_defined_objects, &uobj->base);
    }
}

/**
 * @brief Increments the reference count of an object.
 * @param obj The object whose reference count is to be incremented.
 */
static void inc_ref(object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    assert(uobj->state != ZOMBIE);
    uobj->refs++;
}

/**
 * @brief Decrements the reference count of an object.
 * @param obj The object whose reference count is to be decremented.
 */
static void dec_ref(object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    assert(uobj->state != ZOMBIE);
    if (!(--uobj->refs)) {
        release_or_clear(uobj);
    }
}

/**
 * @brief Marks an object as reachable during garbage collection.
 * @param obj The object to mark as reachable.
 */
static void mark(object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    assert(uobj->state != ZOMBIE);
    uobj->state = MARKED;
}

/**
 * @brief Sweeps the object, cleaning it up or moving it to the object pool.
 * @param obj The object to sweep.
 */
static void sweep(object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    assert(uobj->state != ZOMBIE);
    if (uobj->state == UNMARKED) {
        release_or_clear(uobj);
    } else {
        uobj->state = UNMARKED;
    }
}

/**
 * @brief Releases a user-defined object.
 * @param obj The object to release.
 */
static void release(object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    remove_object_from_list(
        uobj->state == ZOMBIE ? &obj->process->user_defined_objects : &obj->process->objects, obj
    );
    clear(uobj);
    destroy_avl_tree(uobj->children);
    FREE(obj);
}

/**
 * @brief Compares two user-defined objects by their memory addresses.
 * 
 * This function compares the memory addresses of two user-defined objects. The comparison 
 * is based solely on the addresses, not the content of the objects. This ensures a consistent 
 * ordering of user-defined objects in collections, such as AVL trees, where memory address 
 * serves as the criterion for placement.
 * 
 * @param obj1 The first object to compare.
 * @param obj2 The second object to compare.
 * @return An integer indicating the result of the comparison.
 */
static int compare(const object_t *obj1, const object_t *obj2) {
    if (obj1 > obj2) {
        return 1;
    } else if (obj1 > obj2) {
        return -1;
    } else {
        return 0;
    }
}

/**
 * @brief Copies a key-value pair from one AVL tree to another.
 * 
 * This function is used during the cloning of a user-defined object. It increments the reference
 * count of both the key and the value, then inserts the key-value pair into the target AVL tree.
 * 
 * @param collection The target AVL tree to insert the cloned key-value pair.
 * @param key The key of the key-value pair to be cloned.
 * @param value The value of the key-value pair to be cloned.
 */
static void copy_child_pair(void *collection, void *key, value_t value) {
    avl_tree_t *tree = (avl_tree_t *)collection;
    INCREF((object_t *)key);
    INCREF((object_t *)value.ptr);
    set_in_avl_tree(tree, key, value);
}

/**
 * @brief Clones a user-defined object.
 * 
 * This function creates a copy of the given user-defined object. All key-value pairs 
 * in the original object's children AVL tree are copied into the new object's AVL tree. 
 * The reference count of each key and value is incremented.
 * 
 * @param process The process that will own the cloned object.
 * @param obj The user-defined object to be cloned.
 * @return A pointer to the cloned object.
 */
static object_t *clone(process_t *process, object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    object_user_defined_t *copy = create_empty_user_defined_object(process);
    avl_tree_for_each(uobj->children, copy_child_pair, copy->children);
    return &copy->base;
}

/**
 * @brief Converts a key-value pair into a Goat notation string and appends it to the string
 *  builder.
 * 
 * This function processes a single key-value pair from the children of a user-defined object. 
 * It converts both the key and the value to their Goat notation string representations using 
 * their respective `to_string_notation` methods. The resulting key-value pair is appended to 
 * the provided string builder in the format `"key:value"`. If the builder already contains data, 
 * a comma is inserted before appending the new pair.
 * 
 * @param data A pointer to the `string_builder_t` used for constructing the string.
 * @param key The key of the key-value pair, cast to an object.
 * @param value The value of the key-value pair, stored as a `value_t` type.
 */
static void child_pair_to_string(void *data, void *key, value_t value) {
    string_builder_t *builder = (string_builder_t *)data;
    if (builder->length > 1) {
        append_char(builder, ',');
    }
    object_t *key_obj = (object_t *)key;
    string_value_t key_str = key_obj->vtbl->to_string_notation(key_obj);
    append_substring(builder, key_str.data, key_str.length);
    if (key_str.should_free) {
        FREE(key_str.data);
    }
    append_char(builder, ':');
    object_t *value_obj = (object_t *)value.ptr;
    string_value_t value_str = value_obj->vtbl->to_string_notation(value_obj);
    append_substring(builder, value_str.data, value_str.length);
    if (value_str.should_free) {
        FREE(value_str.data);
    }
}

/**
 * @brief Converts a user-defined object to a Goat notation string representation.
 * 
 * This function generates a string representation of a user-defined object in Goat notation, 
 * where each child key-value pair is represented as `"key:value"`. The key-value pairs are 
 * separated by commas and enclosed in curly braces `{}`. The string is constructed dynamically 
 * and must be freed by the caller using `FREE`.

 * @param obj The user-defined object to convert to a string in Goat notation.
 * @return A `string_value_t` structure containing the dynamically allocated string 
 *  representation. The caller is responsible for freeing the memory if `should_free` is true.
 */
static string_value_t to_string_notation(const object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    string_builder_t builder;
    init_string_builder(&builder, 2);
    append_char(&builder, '{');
    avl_tree_for_each(uobj->children, child_pair_to_string, &builder);
    return append_char(&builder, '}');
}

/**
 * @brief Converts a user-defined object to a general string representation.
 * 
 * This function returns a string representation of the user-defined object. 
 * It delegates to the `to_string_notation` function, which formats the object using Goat notation.
 * 
 * @param obj The user-defined object to convert to a string.
 * @return A `string_value_t` structure containing the dynamically allocated string 
 *  representation of the object.
 */
static string_value_t to_string(const object_t *obj) {
    return to_string_notation(obj);
}

/**
 * @brief Retrieves a property value from a user-defined object.
 * 
 * This function looks up the specified `key` in the `children` collection of the user-defined
 * object. If the key exists, the corresponding value is returned. If the key does not exist,
 * `NULL` is returned.
 * 
 * @param obj The user-defined object from which the property is being retrieved.
 * @param key The key for the property to retrieve.
 * @return A pointer to the value associated with the key, or NULL if the key is not found.
 */
static object_t *get_property(object_t *obj, object_t *key) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    return get_from_avl_tree(uobj->children, key).ptr;
}

/**
 * @brief Sets a property value in a user-defined object.
 * 
 * This function adds or updates a property in the `children` collection of the user-defined
 * object. If the key already exists, its value is updated. If the key does not exist, a new
 * key-value pair is added. The reference count of the value is incremented and the count of the
 * previous value (if any) is decremented. 
 * 
 * @param obj The user-defined object to which the property is being set.
 * @param key The key for the property.
 * @param value The value to associate with the key.
 * @return Always returns true, as the property is successfully set or updated.
 */
static bool set_property(object_t *obj, object_t *key, object_t *value) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    INCREF(value);
    value_t old_value = set_in_avl_tree(uobj->children, key, (value_t){ .ptr = value });
    DECREFIF((object_t *)old_value.ptr);
    return true;
}

/**
 * @brief Adds two objects and returns the result.
 * @param process Process that will own the resulting object.
 * @param obj1 The first object.
 * @param obj2 The second object.
 * @return Always returns `false` because addition is not supported for user-defined objects.
 */
static object_t *add(process_t *process, object_t *obj1, object_t *obj2) {
    return false;
}

/**
 * @brief Subtracts one object from another.
 * @param process Process that will own the resulting object.
 * @param obj1 The first object (minuend).
 * @param obj2 The second object (subtrahend).
 * @return Always returns `false` because subtraction is not supported for user-defined objects.
 */
static object_t *sub(process_t *process, object_t *obj1, object_t *obj2) {
    return false;
}

/**
 * @brief Retrieves the boolean representation of a user-defined object.
 * 
 * This function converts the user-defined object to its string representation and returns
 * `true` if the object is non-empty, and `false` otherwise.
 * 
 * @param obj The object from which to retrieve the boolean value.
 * @return `true` if the string is non-empty, `false` if the string is empty.
 */

static bool get_boolean_value(const object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    return uobj->children->root != NULL;
}

/**
 * @brief Retrieves the integer value of a user-defined object.
 * @param obj The user-defined object.
 * @return An invalid `int_value_t` indicating that user-defined objects cannot be converted
 *  to integers.
 */
static int_value_t get_integer_value(const object_t *obj) {
    return (int_value_t){ false, 0 };
}

/**
 * @brief Retrieves the real value of a user-defined object.
 * @param obj The user-defined object.
 * @return An invalid `real_value_t` indicating that user-defined objects cannot be converted
 *  to real numbers.
 */
static real_value_t get_real_value(const object_t *obj) {
    return (real_value_t){ false, 0.0 };
}


/**
 * @var dynamic_string_vtbl
 * @brief Virtual table defining the behavior of the user-defined object.
 */
static object_vtbl_t vtbl = {
    .type = TYPE_STRING,
    .inc_ref = inc_ref,
    .dec_ref = dec_ref,
    .mark = mark,
    .sweep = sweep,
    .release = release,
    .compare = compare,
    .clone = clone,
    .to_string = to_string,
    .to_string_notation = to_string_notation,
    .get_property = get_property,
    .set_property = set_property,
    .add = add,
    .sub = sub,
    .get_boolean_value = get_boolean_value,
    .get_integer_value = get_integer_value,
    .get_real_value = get_real_value
};

/**
 * @brief Compares two keys for use in the user-defined object's AVL tree.
 * 
 * This comparator is used to order keys in the AVL tree of a user-defined object. Keys are first
 * compared by their type (using `vtbl->type`). If the types are equal, the objects are compared
 * using their type-specific `compare` function.
 * 
 * @param first The first key to compare.
 * @param second The second key to compare.
 * @return An integer indicating the result of the comparison:
 *         - `1` if `first` is greater than `second`.
 *         - `-1` if `first` is less than `second`.
 *         - `0` if `first` and `second` are equal.
 */
static int key_comparator(void *first, void *second) {
    object_t *obj1 = (object_t *)first;
    object_t *obj2 = (object_t *)second;
    if (obj1->vtbl->type > obj2->vtbl->type) {
        return 1;
    } else if (obj1->vtbl->type < obj2->vtbl->type) {
        return -1;
    } else {
        return obj1->vtbl->compare(obj1, obj2);
    }
}

static object_user_defined_t *create_empty_user_defined_object(process_t* process) {
    object_user_defined_t *uobj;
    if (process->user_defined_objects.size > 0) {
        uobj = (object_user_defined_t *)remove_first_object_from_list(
            &process->user_defined_objects
        );
    } else {
        uobj = (object_user_defined_t *)CALLOC(sizeof(object_user_defined_t));
        uobj->base.vtbl = &vtbl;
        uobj->base.process = process;
        uobj->children = create_avl_tree(key_comparator);
    }
    uobj->refs = 1;
    uobj->state = UNMARKED;
    add_object_to_list(&process->objects, &uobj->base);
    return uobj;
}

object_t *create_user_defined_object(process_t* process) {
    return &create_empty_user_defined_object(process)->base;
}
