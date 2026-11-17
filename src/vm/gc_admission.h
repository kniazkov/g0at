/**
 * @file gc_admission.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Private, read-only policy for the first GC-10 candidate boundary.
 */

#pragma once

#include "model/process.h"
#include "model/thread.h"
#include "opcodes.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Proven dispatcher provenance, not inferred from lock depth or thread count. */
typedef enum { GC_DISPATCH_UNPROVEN = 0, GC_DISPATCH_STANDALONE_OUTER } gc_dispatch_origin_t;

/** @brief Evidence established by the reviewed dispatcher bridge. */
typedef struct {
    gc_dispatch_origin_t origin;
    uint8_t opcode;
    bool dispatch_continues;  /**< Handler succeeded and execution can continue. */
    bool ordinary_flow;       /**< No unwind or pending transfer is in progress. */
    bool transfers_published; /**< All live managed values have traced owners. */
    bool no_worker_started;   /**< No worker has been started in this execution. */
    bool no_other_clients;    /**< No other client may mutate the model. */
} gc_admission_boundary_t;

/** @brief Select only the reviewed standalone post-ARRAY candidate. */
static inline bool gc_admission_matches_site(gc_admission_boundary_t boundary) {
    return boundary.origin == GC_DISPATCH_STANDALONE_OUTER && boundary.opcode == ARRAY
           && boundary.dispatch_continues && boundary.ordinary_flow && boundary.transfers_published
           && boundary.no_other_clients;
}

/** @brief Original single-participant contract. */
static inline bool gc_admission_matches_boundary(gc_admission_boundary_t boundary) {
    return gc_admission_matches_site(boundary) && boundary.no_worker_started;
}

/**
 * @brief Check the candidate and currently observable VM state without waiting.
 *
 * This checks only the collecting mutator; a separate participant proof is mandatory.
 * It neither establishes dispatcher evidence nor requests a
 * collection. Only the dedicated test dispatcher currently supplies that evidence.
 * Production execution does not call this gate.
 */
static inline bool gc_admission_allows_mutator(const thread_t *thread,
                                               gc_admission_boundary_t boundary) {
    if (!thread || !gc_admission_matches_site(boundary))
        return false;
    const process_t *process = thread->process;
    if (!process || os_mutex_depth(process->execution_mutex) != 1)
        return false;
    return process->execution && process->main_thread == thread && !thread->program_worker
           && !thread->finished && !thread->exception.value && !thread->initialization_depth
           && !process->initializing_file && !process->initialization_waiters
           && !process->finished_workers && !process->exit_requested && !process->native_failed
           && !process->gc_collecting && !process->draining_releases && !process->destroying;
}

/** @brief No other participants have existed in this execution. */
static inline bool gc_admission_allows(const thread_t *thread, gc_admission_boundary_t boundary) {
    return gc_admission_allows_mutator(thread, boundary) && boundary.no_worker_started
           && thread->process->active_threads == 1;
}
