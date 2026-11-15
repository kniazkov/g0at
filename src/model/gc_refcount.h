/**
 * @file gc_refcount.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Checked non-atomic strong counts for process-owned heap objects.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/** @brief Zero denotes an inactive shell or a graph object awaiting finalization. */
typedef uint32_t gc_refcount_t;

/** @brief Result of dropping an ordinary strong reference. */
typedef enum { GC_REFCOUNT_INVALID, GC_REFCOUNT_LIVE, GC_REFCOUNT_LAST } gc_refcount_result_t;

/** @brief Retain an active value without wrapping or resurrecting a zero count. */
static inline bool try_retain_gc_ref(gc_refcount_t *count) {
    if (*count == 0 || *count == UINT32_MAX)
        return false;
    ++*count;
    return true;
}

/** @brief Drop one ordinary reference, reporting last ownership without underflow. */
static inline gc_refcount_result_t try_release_gc_ref(gc_refcount_t *count) {
    if (*count == 0)
        return GC_REFCOUNT_INVALID;
    return --*count == 0 ? GC_REFCOUNT_LAST : GC_REFCOUNT_LIVE;
}

/** @brief Mutating an invalid live count is a runtime invariant failure, in all builds. */
static inline void retain_gc_ref(gc_refcount_t *count) {
    if (!try_retain_gc_ref(count))
        abort();
}

/** @brief True only on the ordinary last-owner transition. */
static inline bool release_gc_ref(gc_refcount_t *count) {
    gc_refcount_result_t result = try_release_gc_ref(count);
    if (result == GC_REFCOUNT_INVALID)
        abort();
    return result == GC_REFCOUNT_LAST;
}

/** @brief Sweep drops only an excess reference, protecting the marked owner. */
static inline bool drop_swept_gc_ref(gc_refcount_t *count) {
    if (*count <= 1)
        return false;
    --*count;
    return true;
}
