/**
 * @file object.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines the base structure for all objects in the Goat programming language.
 * 
 * This file describes the general framework for objects in the Goat language. 
 * In Goat, all entities, including primitive types like strings and numbers, 
 * as well as functions, are considered objects. The specific characteristics of 
 * each object type differ, but here we define the common structure that all 
 * objects in the language will share.
 */

#pragma once

#include "lib/value.h"

/**
 * @typedef object_t
 * @brief Forward declaration for the object structure.
 */
typedef struct object_t object_t;

/**
 * @typedef process_t
 * @brief Forward declaration for the process structure.
 */
typedef struct process_t process_t;

/**
 * @enum object_type_t
 * @brief Enumeration of object types in the Goat virtual machine.
 *
 * Each object type represents a category of objects that may be used by the virtual machine.
 * The types are utilized in comparison operations to determine the relative ordering of objects
 * and ensure correct behavior in data structures like the AVL tree.
 *
 * Types are used to compare objects in the `compare_objects` function. The comparison first
 * checks the type, and if the types are equal, it compares the content of the objects.
 */
typedef enum {
    /**
     * @brief Boolean type (true/false).
     */
    TYPE_BOOLEAN = 0,

    /**
     * @brief Numeric type (integer or floating-point numbers).
     */
    TYPE_NUMBER = 1,

    /**
     * @brief String type (sequence of characters).
     */
    TYPE_STRING = 2,

    /**
     * @brief User-defined object type (for objects created by the user).
     */
    TYPE_USER_DEFINED_OBJECT = 3,

    /**
     * @brief User-defined object type (for objects created by the user).
     */
    TYPE_OTHER = 4
} object_type_t;

/**
 * @struct object_vtbl_t
 * @brief The virtual table structure for objects in Goat.
 * 
 * This structure contains pointers to functions that implement the behavior
 * of different object types in the Goat language. The virtual table is used
 * to enable polymorphism, allowing different object types to have their own
 * implementations for certain operations.
 */
typedef struct {
    /**
     * @brief The type of the object.
     * 
     * This field specifies the type of the object. It is used to differentiate between
     * different object types in the virtual machine.
     * Since the type is consistent across all objects of a given type, it is included in the
     * virtual table rather than in each individual object. This approach allows for a more
     * compact memory layout without adding redundant fields to each object.
     * 
     * @note This is not a function pointer, but is included in the virtual table to align with
     *  the overall structure and avoid adding redundant type fields to each object.
     */    
    object_type_t type;

    /**
     * @brief Function pointer for adding a reference to an object (incrementing its
     *  reference count).
     * 
     * This function increments the reference count of the object to indicate that
     * it is being referenced by another part of the program.
     * 
     * @param obj The object to add a reference to.
     */
    void (*inc_ref)(object_t *obj);

    /**
     * @brief Function pointer for removing a reference from an object (decrementing its
     *  reference count).
     * 
     * This function decrements the reference count of the object. When the reference count
     * reaches zero, the object is eligible immediate destruction.
     * 
     * @param obj The object to remove a reference from.
     */
    void (*dec_ref)(object_t *obj);

    /**
     * @brief Function pointer for marking an object during garbage collection.
     * 
     * The `mark` function is used during the garbage collection process to identify
     * objects that are still in use. This function typically sets a flag or performs
     * some other operation to indicate that the object is reachable from the root set
     * or another marked object.
     * 
     * @param obj The object to mark.
     */
    void (*mark)(object_t *obj);

    /**
     * @brief Function pointer for sweeping (cleaning up) an object during garbage collection.
     * 
     * The `sweep` function is responsible for cleaning up objects that are no longer
     * marked as in use. If an object is determined to be unreachable, this function
     * handles its destruction, releasing any resources it holds. This method ensures
     * that objects are properly finalized and removed from memory.
     * 
     * @param obj The object to sweep.
     */
    void (*sweep)(object_t *obj);

    /**
     * @brief Function pointer for releasing (destroying) an object.
     * @param obj The object to release.
     */
    void (*release)(object_t *obj);

    /**
     * @brief Function pointer for comparing two objects.
     * 
     * This function compares two objects and determines their relative ordering. It is used
     * in data structures such as AVL trees to maintain proper ordering of objects based on
     * user-defined criteria.
     * 
     * The function returns:
     * - A negative integer if `obj1` is less than `obj2`.
     * - Zero if `obj1` is equal to `obj2`.
     * - A positive integer if `obj1` is greater than `obj2`.
     * 
     * @param obj1 The first object to compare.
     * @param obj2 The second object to compare.
     * @return An integer indicating the relative order of the two objects.
     * 
     * @note The comparison criteria should be consistent and transitive to ensure correctness
     *  of data structures like AVL trees that rely on this function.
     */
    int (*compare)(const object_t *obj1, const object_t *obj2);

    /**
     * @brief Function pointer for converting an object to its string representation.
     * @param obj The object to convert to a string.
     * @return The string representation of the object as a `string_value_t`.
     * @note The returned string is dynamically allocated, and the caller must ensure that
     *  the memory is freed after use to avoid memory leaks. The `should_free` flag in the 
     *  `string_value_t` structure indicates whether the caller should free the memory.
     */
    string_value_t (*to_string)(const object_t *obj);

    /**
     * @brief Function pointer for converting an object to its Goat notation representation.
     * @param obj The object to convert to Goat notation.
     * @return The Goat notation string representation of the object as a `string_value_t`.
     * @note The returned string is dynamically allocated, and the caller must ensure that
     *  the memory is freed after use to avoid memory leaks. The `should_free` flag in the 
     *  `string_value_t` structure indicates whether the caller should free the memory.
     */
    string_value_t (*to_string_notation)(const object_t *obj);

    /**
     * @brief Function pointer for adding two objects.
     * 
     * The `add` function is used for executing the `ADD` operation, which adds the values
     * of two objects and returns a new object representing the result.
     * 
     * @param process Pointer to the process that will own the resulting object.
     * @param obj1 Pointer to the first object to add.
     * @param obj2 Pointer to the second object to add.
     * @return A pointer to the resulting object of the addition.
     */
    object_t* (*add)(process_t *process, object_t *obj1, object_t *obj2);

    /**
     * @brief Function pointer for subtracting two objects.
     * 
     * The `sub` function is used for executing the `SUB` operation, which subtracts the value
     * of the second object from the first and returns a new object representing the result.
     * 
     * @param process Pointer to the process that will own the resulting object.
     * @param obj1 Pointer to the first object (minuend).
     * @param obj2 Pointer to the second object (subtrahend).
     * @return A pointer to the resulting object of the subtraction.
     */
    object_t* (*sub)(process_t *process, object_t *obj1, object_t *obj2);

    /**
     * @brief Function pointer for retrieving the boolean value of an object.
     * 
     * The `get_boolean_value` function retrieves the boolean representation of an object. 
     * Every object in the system can be converted to a boolean value. For example, zero, 
     * `null`, or empty values might be considered `false`, while others are `true`.
     * 
     * @param obj The object to convert to a boolean value.
     * @return Boolean representation of the object.
     */
    bool (*get_boolean_value)(const object_t *obj);

    /**
     * @brief Function pointer for retrieving the integer value of an object.
     * 
     * The `get_integer_value` function is used to retrieve the integer value of an object, 
     * if the object can be logically represented as an integer. If the object cannot be converted
     * to an integer, this function returns an `int_value_t` with `has_value` set to `false` 
     * and `value` set to an undefined state.
     * 
     * @param obj The object from which to retrieve the integer value.
     * @return An `int_value_t` structure containing the integer value, or indicating that the 
     *  value is not available if `has_value` is `false`.
     */
    int_value_t (*get_integer_value)(const object_t *obj);

    /**
     * @brief Function pointer for retrieving the floating-point value of an object.
     * 
     * The `get_real_value` function retrieves the floating-point (real) value of an object, 
     * if the object can be logically represented as a real number. If the object cannot be 
     * converted to a real number, this function returns a `real_value_t` with `has_value` set 
     * to `false` and `value` set to an undefined state.
     * 
     * @param obj The object from which to retrieve the floating-point value.
     * @return A `real_value_t` structure containing the floating-point value, or indicating 
     *  that the value is not available if `has_value` is `false`.
     */
    real_value_t (*get_real_value)(const object_t *obj);
} object_vtbl_t;

/**
 * @struct object_t
 * @brief The base object structure in Goat.
 * 
 * This structure represents the base object in the Goat programming language.
 * All objects, whether primitive types, functions, or other user-defined types,
 * share this common structure, which includes a pointer to their virtual table.
 * The virtual table enables polymorphic behavior for objects of different types.
 */
struct object_t {
    /**
     * @brief Pointer to the object's virtual table.
     */
    object_vtbl_t *vtbl;

    /**
     * @brief Pointer to the process that owns this object.
     * 
     * Each object is associated with a process that manages its lifetime. The process is 
     * responsible for memory management, including garbage collection and object destruction.
     */
    process_t *process;

    /**
     * @brief Pointer to the previous object in the list.
     * 
     * This pointer references the object that precedes the current object in
     * the doubly linked list.
     */
    object_t *previous;

    /**
     * @brief Pointer to the next object in the list.
     * 
     * This pointer references the object that follows the current object in
     * the doubly linked list.
     */
    object_t *next;    
};

/**
 * @brief Macro to increment the reference count of an object.
 * 
 * This macro increments the reference count of the object by calling the
 * `inc_ref` function through the object's virtual table.
 * 
 * @param obj The object whose reference count is to be incremented.
 */
#define INCREF(obj)  ((obj)->vtbl->inc_ref(obj))

/**
 * @brief Macro to decrement the reference count of an object.
 * 
 * This macro decrements the reference count of the object by calling the
 * `dec_ref` function through the object's virtual table. If the reference
 * count reaches zero, the object is released or cleared.
 * 
 * @param obj The object whose reference count is to be decremented.
 */
#define DECREF(obj)  ((obj)->vtbl->dec_ref(obj))

/**
 * @brief Retrieves the singleton instance for a given boolean value.
 * @param value The boolean value.
 * @return A pointer to the singleton object representing `true` or `false`.
 */
object_t *get_boolean_object(bool value);

/**
 * @brief Creates a new integer object.
 * @param process The process to which the object will belong.
 * @param value The integer value to store in the object.
 * @return A pointer to the newly created object, which is of type `object_t`.
 */
object_t *create_integer_object(process_t *process, int64_t value);

/**
 * @brief Creates a new static string object.
 * 
 * This function creates a new string object with the provided string data. The object is
 * dynamically allocated but is not managed by the garbage collector. The memory for the object
 * is expected to be manually freed when the process ends.
 * 
 * @param data The string data for the static string.
 * @param length The length of the string.
 * @return A pointer to the newly created static string object.
 * @note The memory for the static string object is allocated dynamically, but it is not
 *       managed by the garbage collector and must be manually freed when the process terminates.
 */
object_t *create_static_string_object(wchar_t *data, size_t length);

/**
 * @brief Creates a dynamic string object from a string value.
 * @param process The process in which the object is created.
 * @param value The string value to use for creating the dynamic string.
 * @return A pointer to the created dynamic string object.
 */
object_t *create_dynamic_string_object(process_t *process, string_value_t value);

/**
 * @brief Macro to declare a getter function for a static object.
 * @param name The name of the static object for which the getter is generated.
 */
#define DECLARE_STATIC_OBJECT_GETTER(name) \
    object_t *get_##name();

/**
 * @brief Declares getter functions for common static string objects.
 */
DECLARE_STATIC_OBJECT_GETTER(empty_string)
DECLARE_STATIC_OBJECT_GETTER(string_print)
