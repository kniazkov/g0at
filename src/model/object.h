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

#include <wchar.h>
#include <stdint.h>

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
     * @brief Function pointer for converting an object to its string representation.
     * @param obj The object to convert to a string.
     * @return The string representation of the object.
     */
    wchar_t* (*to_string)(object_t *obj);

    /**
     * @brief Function pointer for converting an object to its Goat notation representation.
     * @param obj The object to convert to Goat notation.
     * @return The Goat notation string representation of the object.
     */
    wchar_t* (*to_string_notation)(object_t *obj);

    /**
     * @brief Function pointer for adding two objects.
     * 
     * The `add` function is used for executing the `ADD` operation, which adds the values
     * of two objects and returns a new object representing the result.
     * 
     * @param obj1 Pointer to the first object to add.
     * @param obj2 Pointer to the second object to add.
     * @return A pointer to the resulting object of the addition.
     */
    object_t* (*add)(object_t *obj1, object_t *obj2);

    /**
     * @brief Function pointer for subtracting two objects.
     * 
     * The `sub` function is used for executing the `SUB` operation, which subtracts the value
     * of the second object from the first and returns a new object representing the result.
     * 
     * @param obj1 Pointer to the first object (minuend).
     * @param obj2 Pointer to the second object (subtrahend).
     * @return A pointer to the resulting object of the subtraction.
     */
    object_t* (*sub)(object_t *obj1, object_t *obj2);

    /**
     * @brief Function pointer for retrieving the integer value of an object.
     * 
     * The `get_integer_value` function is used to retrieve the integer value of an object, 
     * if the object can be logically represented as an integer. If the object cannot be converted
     * to an integer, this function returns `NULL`.
     * 
     * @param obj The object from which to retrieve the integer value.
     * @return A pointer to the integer value, or `NULL` if the object cannot be interpreted
     *  as an integer.
     */
    const int64_t* (*get_integer_value)(object_t *obj);
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
 * @brief Creates a new integer object.
 * @param process The process to which the object will belong.
 * @param value The integer value to store in the object.
 * @return A pointer to the newly created object, which is of type `object_t`.
 */
object_t *create_integer_object(process_t *process, int64_t value);