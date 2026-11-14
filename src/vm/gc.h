/**
 * @file gc.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Garbage Collection for the Goat Virtual Machine.
 *
 * Reference counting handles ordinary ownership, while tracing reclaims cycles.
 * Roots include process/module state, thread startup slots and execution state,
 * not arbitrary local C variables. Explicit registration is a tracing root, not
 * a substitute for retaining an owned reference.
 * @see add_gc_root, remove_gc_root
 */

#pragma once

#include "model/process.h"

/**
 * @brief Traces and sweeps the specified process at a caller-established safe point.
 *
 * The collector does not acquire the execution lock. During execution the caller
 * must hold that lock and publish all live references into traced state first.
 * Out of execution, exclusive ownership of the process suffices. Recursive and
 * concurrent collection on the same process are unsupported.
 */
void collect_garbage(process_t *proc);

/**
 * @brief Registers an external object as a garbage-collection root.
 *
 * Register a live object of this process (or a process-independent static object).
 * Registration is set-like and does not retain the object. Keep an owned reference
 * until after unregistering; serialize updates with process model operations.
 */
void add_gc_root(process_t *proc, object_t *obj);

/** @brief Unregisters an external object from the garbage-collection roots. */
void remove_gc_root(process_t *proc, object_t *obj);
