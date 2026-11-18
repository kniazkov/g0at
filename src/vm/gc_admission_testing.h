/** @file gc_admission_testing.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Private API of the separately compiled GC dispatcher test host only.
 */
#pragma once
#ifndef GOAT_TEST_GC_DISPATCH
#    error "GC dispatcher probes are only available in the dedicated test host"
#endif
#include "gc.h"
#include "vm.h"

/** @brief One run-scoped request at the first post-ARRAY boundary, including refusal.
 * The host must own the process and all inputs exclusively for the complete run.
 * Set exclusive_model_client false to test refusal; the VM cannot infer this promise.
 * Outputs are reset on entry, even when run cannot start. No request is stored in process_t.
 */
typedef struct {
    bool exclusive_model_client;
    bool attempted;
    bool collected;
    bool published_workers; /**< Opt in to the controlled fixture publication protocol. */
} gc_dispatch_test_t;

/** @brief Execute a standalone run with at most one test collection attempt. */
int run_with_gc_dispatch_test(process_t *proc, bytecode_t *code, gc_dispatch_test_t *request);

/** @brief Stack-scoped fixture registration; zero initialize, never copy or modify while linked.
 * This API is ONLY for reviewed test builtins: static callable, explicitly published arguments, no
 * receiver, no enclosing native/module frames except explicitly rooted test builtin callers.
 * All live managed C-local values must be included in the batch or caller roots. The caller keeps
 * its own reference. Register under lock depth one before waiting; unregister under the same lock
 * before touching the graph or returning/throwing. Waiting alone, including a spurious wakeup, is
 * not permission to mutate the model.
 */
typedef struct gc_test_park_t {
    thread_t *thread;
    struct gc_test_park_t *next;
    gc_root_handle_t *roots;
    size_t root_count;
} gc_test_park_t;

/** @brief Validate the entire nonempty batch before publishing any root or stable state. */
bool gc_test_publish_park_values(thread_t *thread, object_array_t values, gc_test_park_t *park);

bool gc_test_publish_park(thread_t *thread, object_t *value, gc_test_park_t *park);
bool gc_test_resume(thread_t *thread, gc_test_park_t *park);
