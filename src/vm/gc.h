/**
 * @file gc.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Garbage Collection for the Goat Virtual Machine.
 *
 * Reference counting handles ordinary ownership, while tracing reclaims cycles.
 * Roots include process/module state and every thread context/stack, not arbitrary
 * local C variables. External host references must use the explicit root API when
 * they need protection from collection.
 * @see add_gc_root, remove_gc_root
 */

#pragma once

#include "model/process.h"

/** @brief Performs garbage collection on the specified process. */
void collect_garbage(process_t *proc);

/**
 * @brief Registers an external object as a garbage-collection root.
 *
 * The object becomes reachable from the collector and will not be swept until it is
 * unregistered or the process is destroyed.
 */
void add_gc_root(process_t *proc, object_t *obj);

/** @brief Unregisters an external object from the garbage-collection roots. */
void remove_gc_root(process_t *proc, object_t *obj);
