/** @file gc_policy.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Opt-in process-local allocation budgets; never collect from an allocation hook.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef struct process_t process_t;
typedef struct object_t object_t;

#define GC_POLICY_DEFAULT_MIN_BYTES ((size_t)64 * 1024)
#define GC_POLICY_MAX_BACKOFF 4

/** @brief Serialized by model ownership, like the heap; all counters saturate. */
typedef struct {
    bool enabled, pending;
    size_t minimum_bytes, threshold_bytes, allocated_bytes, live_bytes;
    size_t allocation_events, collections;
    unsigned backoff;
} gc_policy_t;

/** @brief Configure/reset while idle under the model lock. Zero disables and clears.
 * Enabling requires exclusive host ownership for every run. No CLI/default opt-in.
 * Debt and pending requests otherwise survive session boundaries, until collection.
 */
bool configure_gc_policy(process_t *process, size_t minimum_bytes, bool exclusive_host);

/** @brief Charge logical activation/capacity growth, without allocation, publication or tracing.
 * Caller holds the model lock or exclusively owns the idle process. Reused shells
 * count again; decrefs/frees do not cancel allocation pressure. Zero charges nothing.
 */
void gc_policy_note_allocation(process_t *process, size_t bytes);
/** @brief Snapshot direct owned payload for an enabled policy; otherwise zero. */
size_t gc_policy_object_bytes(const object_t *object);
/** @brief Charge positive direct-payload growth since the snapshot, excluding child values. */
void gc_policy_note_growth(object_t *object, size_t before);
/** @brief Internal completed-pass feedback, including explicit and run-end collections. */
void gc_policy_collected(process_t *process, size_t before_bytes, size_t live_bytes);
