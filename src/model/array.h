/** @file array.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Internal array storage and operations; not part of the native ABI.
 */
#pragma once
#include "object.h"
#include "object_state.h"

/** @brief An array owns its elements and a cached immutable length property. */
typedef struct {
    object_t base;        /**< Object header. */
    size_t refs;          /**< Strong reference count. */
    object_state_t state; /**< Tracing/destruction state. */
    object_t **items;     /**< Owned element references. */
    size_t count;         /**< Current element count. */
    size_t capacity;      /**< Allocated element slots. */
    object_t *length;     /**< Owned property value. */
} array_object_t;

/** @brief Sets count and replaces the owned length property. */
void set_array_count(array_object_t *array, size_t count);
/** @brief Grows storage geometrically; fatal allocation failure follows ALLOC policy. */
void reserve_array(array_object_t *array, size_t count);
/** @brief Array prototype method names. */
object_array_t get_array_method_keys(void);
/** @brief Finds an Array method by string key, or returns NULL. */
object_t *get_array_method(const object_t *key);
