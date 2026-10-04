/** @file native_execution.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Native execution policy and optional reports.
 */
#pragma once
#include "graph/node.h"
#include "options.h"
#include "vm/bytecode.h"
typedef struct process_t process_t;

typedef struct {
    const char *preparation;
    size_t bound_functions;
    size_t omitted_specializations;
} native_execution_report_t;

/** @brief Applies preparation policy; false prevents program execution. */
bool prepare_native_program(const options_t *options,
                            const node_t *root,
                            bytecode_t *code,
                            native_execution_report_t *report);
/** @brief Writes requested counters; process may be NULL when preparation failed. */
bool output_native_report(const options_t *options,
                          const native_execution_report_t *report,
                          const process_t *process);

bool prepare_native_program_artifact(const options_t *options,
                                     const node_t *root,
                                     bytecode_t *code,
                                     native_execution_report_t *report,
                                     const char *destination);
