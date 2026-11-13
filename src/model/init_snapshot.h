/** @file init_snapshot.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Internal, fully validated constructor selection; never invokes user code.
 */
#pragma once

#include "object.h"

typedef struct init_snapshot_t init_snapshot_t;

/** @brief Owns either a complete snapshot or an exception, never a partial snapshot. */
typedef struct {
    init_snapshot_t *snapshot;
    exception_t exception;
} init_snapshot_result_t;

/** @brief Collects own init functions on real ancestors, in reverse lookup order.
 * Borrows receiver. The receiver itself and lexical enclosing scopes are excluded.
 * Missing fields are skipped; any present non-function produces InvalidOperation.
 * Access modifiers are ignored during selection, without granting invocation rights.
 * No calls or GC safepoints occur during collection. Use under the process model lock.
 * Success retains and roots receiver and selected functions until destroy_init_snapshot;
 * the process must outlive the snapshot. Failure owns only its exception.
 */
init_snapshot_result_t collect_init_snapshot(process_t *process, object_t *receiver);

/** @brief Borrows the retained receiver. */
object_t *get_init_snapshot_receiver(const init_snapshot_t *snapshot);
/** @brief Borrows immutable selected function pointers, valid until snapshot destruction. */
object_array_t get_init_snapshot_functions(const init_snapshot_t *snapshot);
/** @brief Unregisters the GC root and releases all references. NULL is allowed. */
void destroy_init_snapshot(init_snapshot_t *snapshot);
