/**
 * @file object_state.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines the object states for garbage collection in the Goat language.
 */

#pragma once

/** @brief The different states of an object in the Goat language. */
typedef enum {
    /** @brief The object is not marked for garbage collection. */
    UNMARKED = 0,

    /** @brief The object is marked for garbage collection. */
    MARKED = 1,

    /** @brief The object is in the process of being destroyed or cleaned up. */
    DYING = 2,

    /** @brief The object has been moved to the object pool. */
    ZOMBIE = 3
} object_state_t;
