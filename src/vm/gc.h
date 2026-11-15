/**
 * @file gc.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Garbage Collection for the Goat Virtual Machine.
 *
 * Reference counting handles ordinary ownership, while tracing reclaims cycles.
 * Roots include process/module state, thread startup slots and execution state,
 * not arbitrary local C variables. An explicit registration both owns
 * a reference and makes it a tracing root.
 * @see register_gc_root, unregister_gc_root
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
 * @brief Value token for one registration (opaque to clients; zero is invalid).
 *
 * Copying a token does not create another registration. Tokens do not extend
 * process lifetime. Never pass a destroyed process; a token from another
 * process, including a previous process lifetime, cannot remove a live root.
 */
typedef struct {
    uint64_t process_id;
    uint64_t serial;
    size_t index;
} gc_root_handle_t;

/**
 * @brief Register and retain a live object, independently of other handles to it.
 *
 * Accepts same-process objects and process-independent static values. NULL,
 * foreign objects, teardown, collection and an in-progress release drain return an invalid
 * handle without retaining anything. Growth follows the managed allocator's
 * fatal allocation policy. Updates use the same locking as the object model:
 * hold the execution lock or have exclusive process ownership.
 */
gc_root_handle_t register_gc_root(process_t *proc, object_t *obj);

/**
 * @brief Remove only this registration and drop its ownership, without allocating.
 * Invalid, stale, repeated and foreign handles, and attempts during collection,
 * return false with no effect.
 * The object can be released synchronously. Uses the same locking as registration.
 */
bool unregister_gc_root(process_t *proc, gc_root_handle_t handle);

/** @brief Internal teardown hook, once registration is closed and before heap disposal. */
void clear_gc_roots(process_t *proc);
