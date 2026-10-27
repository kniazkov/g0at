/** @file inspect_probe.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Bounded native-load trials; process isolation is not a security sandbox.
 */
#pragma once
#include "lib/process/cleanup_service.h"
#include "module/module_container.h"

typedef enum {
    INSPECT_PROBE_PASSED,
    INSPECT_PROBE_FAILED,
    INSPECT_PROBE_CRASHED,
    INSPECT_PROBE_TIMEOUT
} inspect_probe_state_t;

typedef enum {
    INSPECT_PROBE_START,
    INSPECT_PROBE_LOAD,
    INSPECT_PROBE_DESCRIPTOR,
    INSPECT_PROBE_SYMBOLS,
    INSPECT_PROBE_UNLOAD,
    INSPECT_PROBE_EXIT,
    INSPECT_PROBE_CAPTURE
} inspect_probe_stage_t;

typedef struct {
    inspect_probe_state_t state;
    inspect_probe_stage_t stage;
    bool load_completed, descriptor_completed, symbols_completed, unload_completed;
    bool abi_error, infrastructure_error;
    cleanup_status_t service_status;
    child_result_t child;
    char diagnostic[512];
} inspect_probe_result_t;

/** Caller must validate the WHOLE graph before calling. Container is the retained
 * validated snapshot, not a reread path. Service is shared across all nodes;
 * caller finishes/destroys it exactly once and reports cleanup independently.
 * The result is large: prefer heap allocation. No native code runs in the caller.
 */
void inspect_probe_module(cleanup_service_t *service,
                          const goat_module_container *container,
                          size_t variant_index,
                          size_t ordinal,
                          uint64_t timeout_ms,
                          inspect_probe_result_t *result);
/** Reserved child entrypoint, before normal option handling. */
bool inspect_probe_main(int argc, char **argv, int *exit_code);
const char *inspect_probe_state_name(inspect_probe_state_t state);
const char *inspect_probe_stage_name(inspect_probe_stage_t stage);
