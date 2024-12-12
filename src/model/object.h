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
 * @brief Creates a new integer object.
 * @param process The process to which the object will belong.
 * @param value The integer value to store in the object.
 * @return A pointer to the newly created object, which is of type `object_t`.
 */
object_t *create_integer_object(process_t *process, int64_t value);