/** @file merge_terminal.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Real-terminal author selection with scenario-owned cleanup and artifacts.
 */
#pragma once
#include "test/support/execution.h"

/** argv includes the executable. Run in the scenario directory, await the author
 * prompt for at most ten seconds, invoke action, then send response through an
 * echo-disabled PTY (including literal Ctrl-D). The master remains open until
 * completion, bounded by ten seconds and the shared deadline. Both streams are
 * returned and persisted. Action failures still stop/reap the owned process.
 * POSIX only: callers must explicitly skip terminal scenarios on Windows.
 */
scenario_command_result_t merge_terminal(scenario_run_t *run,
                                         const char *const *argv,
                                         scenario_body_t action,
                                         void *user,
                                         const void *response,
                                         size_t response_size);
