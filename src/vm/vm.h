/**
 * @file vm.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines the virtual machine for the Goat programming language.
 */

#pragma once

#include "bytecode.h"
#include "model/module_files.h"
#include "model/process.h"

/**
 * @brief Runs bytecode in proc; the caller retains both objects.
 * The process is initialized automatically if needed.
 * Returns nonzero on uncaught exception or invalid exception bytecode.
 * Uncaught values remain in the originating thread's exception field; other diagnostics are
 * incomplete.
 */
int run(process_t *proc, bytecode_t *code);

/** @brief Execute one prepared module file through its once-only initialization gate.
 * The process retains the persistent scope and original failure. Included files
 * execute synchronously using fixed local links and never reopen source paths.
 * The caller frame, instruction pointer and operands survive this outer VM entry.
 * On failure the originating exception is stored in the main thread, as for run.
 * Not a reentrant VM entry: invoke only when no run is active in this process.
 */
int run_module_file(process_t *proc, module_file_t *file);
