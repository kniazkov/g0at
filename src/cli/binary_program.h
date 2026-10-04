/** @file binary_program.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Separate compilation and execution entry points.
 */
#pragma once
#include "native_execution.h"

bool compile_binary_program(const options_t *options, const node_t *root, bytecode_t *code);
int run_binary_program(const options_t *options);
/** @brief Shared execution and exception reporting for source and compiled inputs. */
int execute_program(const options_t *options,
                    bytecode_t *code,
                    const native_execution_report_t *report);

/** @brief Rejects artifact/report collisions before any output is written. */
bool binary_program_options_valid(const options_t *options);
