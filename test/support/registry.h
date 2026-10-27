/** @file registry.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Stable test scenario registry and direct command-line selection.
 */
#pragma once
#include "test/support/execution.h"

/** @brief One independent scenario; metadata strings remain borrowed.
 * IDs use ASCII letters/digits with '.', '_' and '-' separators, starting with a letter.
 * CTest owns labels and resource constraints; the C registry owns execution only.
 * A nonempty skip_reason explicitly skips the scenario (exit 77); NULL means runnable.
 * Bodies obey scenario_execute's cooperative execution contract. Use a supervised host
 * for potentially uncooperative test code. timeout_ms is mandatory even for skipped cases.
 */
typedef struct {
    const char *id;
    uint64_t timeout_ms;
    scenario_body_t body;
    void *user;
    const char *skip_reason;
} scenario_definition_t;

/** @brief Validate all IDs, uniqueness, deadlines and runnable bodies. */
bool scenario_registry_validate(const scenario_definition_t *definitions,
                                size_t count,
                                FILE *diagnostics);
/** @brief Dispatch exactly one ID, or print IDs with --list.
 * argv excludes the executable name: <id> [--keep-work] or --list.
 * Returns 0 for success, 1 for failure/invalid selection, and 77 for an explicit skip.
 * Every invocation is independent: failure never changes the registry or later calls.
 * base_options supplies existing build/data paths and diagnostics; its name is ignored.
 */
int scenario_registry_main(const scenario_definition_t *definitions,
                           size_t count,
                           int argc,
                           const char *const *argv,
                           const scenario_options_t *base_options);
