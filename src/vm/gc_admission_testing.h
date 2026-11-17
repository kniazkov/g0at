/** @file gc_admission_testing.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Private API of the separately compiled GC dispatcher test host only.
 */
#pragma once
#ifndef GOAT_TEST_GC_DISPATCH
#    error "GC dispatcher probes are only available in the dedicated test host"
#endif
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
} gc_dispatch_test_t;

/** @brief Execute a standalone run with at most one test collection attempt. */
int run_with_gc_dispatch_test(process_t *proc, bytecode_t *code, gc_dispatch_test_t *request);
