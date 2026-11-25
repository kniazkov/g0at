/** @file gc_policy.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Allocation hysteresis and bounded backoff for low-yield tracing passes.
 */
#include "gc_policy.h"

#include "gc_stats.h"
#include "model/object.h"
#include "model/process.h"

#include <stdint.h>

static size_t add(size_t a, size_t b) {
    return b > SIZE_MAX - a ? SIZE_MAX : a + b;
}

static void set_threshold(gc_policy_t *policy) {
    size_t budget = policy->live_bytes / 2;
    if (budget < policy->minimum_bytes)
        budget = policy->minimum_bytes;
    for (unsigned i = 0; i < policy->backoff; ++i)
        budget = add(budget, budget);
    policy->threshold_bytes = budget;
}

bool configure_gc_policy(process_t *process, size_t minimum, bool exclusive) {
    if (!process || !os_mutex_depth(process->execution_mutex) || process->execution
        || process->gc_session || process->destroying || process->gc_collecting
        || process->draining_releases || process->gc_pressure.pending || (minimum && !exclusive))
        return false;
    process->gc_policy = (gc_policy_t){0};
    if (minimum) {
        gc_policy_t *policy = &process->gc_policy;
        policy->enabled = true;
        policy->minimum_bytes = minimum;
        policy->live_bytes = gc_measure_memory(process).active_bytes;
        set_threshold(policy);
    }
    return true;
}

void gc_policy_note_allocation(process_t *process, size_t bytes) {
    if (!process || !process->gc_policy.enabled || !bytes || process->destroying
        || process->gc_collecting)
        return;
    gc_policy_t *policy = &process->gc_policy;
    policy->allocated_bytes = add(policy->allocated_bytes, bytes);
    policy->allocation_events = add(policy->allocation_events, 1);
    if (policy->allocated_bytes >= policy->threshold_bytes)
        policy->pending = true;
}

size_t gc_policy_object_bytes(const object_t *object) {
    return object && object->process && object->process->gc_policy.enabled
                   && object->vtbl->gc_owned_payload
               ? object->vtbl->gc_owned_payload(object)
               : 0;
}

void gc_policy_note_growth(object_t *object, size_t before) {
    size_t after = gc_policy_object_bytes(object);
    if (after > before)
        gc_policy_note_allocation(object->process, after - before);
}

void gc_policy_collected(process_t *process, size_t before, size_t live) {
    gc_policy_t *policy = &process->gc_policy;
    if (!policy->enabled)
        return;
    size_t reclaimed = before >= live ? before - live : 0;
    /* At least one eighth of active payload must disappear. Round the required
       yield upward without multiplication or overflow, including tiny heaps. */
    size_t useful = before / 8 + (before % 8 != 0);
    if (reclaimed && reclaimed >= useful) {
        if (policy->backoff)
            --policy->backoff;
    } else if (policy->backoff < GC_POLICY_MAX_BACKOFF) {
        ++policy->backoff;
    }
    policy->collections = add(policy->collections, 1);
    policy->live_bytes = live;
    policy->allocated_bytes = 0;
    policy->pending = false;
    set_threshold(policy);
}

bool request_gc_pressure(process_t *process) {
    if (!process || !os_mutex_depth(process->execution_mutex) || !process->gc_policy.enabled
        || process->gc_collecting || process->draining_releases || process->destroying
        || process->exit_requested || process->native_failed)
        return false;
    process->gc_pressure.pending = true;
    return true;
}
