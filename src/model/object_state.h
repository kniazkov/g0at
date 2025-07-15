/**
 * @file object_state.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines the object states for garbage collection in the Goat language.
 * 
 * This file describes the `object_state_t` enum, which is used to track the current state
 * of objects in the Goat programming language. The state of an object is crucial for the
 * garbage collection process, as it helps determine whether an object is active, marked,
 * or has been moved to the object pool for future reuse.
 */

#pragma once

/**
 * @enum object_state_t
 * @brief Represents the different states of an object in the Goat language.
 * 
 * This enum is used to track the current state of an object during its lifecycle.
 * The state helps the garbage collector determine the object's status, such as whether it needs
 * to be marked, swept, or placed in the object pool for reuse.
 */
typedef enum {
    /**
     * @brief The object is not marked for garbage collection.
     * 
     * This state indicates that the object is currently not reachable and should be considered
     * for garbage collection. It has not been marked as in use.
     */
    UNMARKED = 0,

    /**
     * @brief The object is marked for garbage collection.
     * 
     * This state indicates that the object has been marked as reachable by the garbage collector.
     * It is in use and will not be swept or destroyed in the current garbage collection cycle.
     */
    MARKED = 1,

    /**
     * @brief The object is in the process of being destroyed or cleaned up.
     * 
     * This state indicates that the object's destruction or cleanup process has begun but hasn't
     * completed yet. This prevents recursive destruction attempts that could lead to segmentation
     * faults or other issues if the object is encountered again during the cleanup process.
     */
    DYING = 2,

    /**
     * @brief The object has been moved to the object pool.
     * 
     * This state means the object has been removed from active use but instead of being destroyed,
     * it has been placed in the object pool for potential reuse.
     */
    ZOMBIE = 3
} object_state_t;
