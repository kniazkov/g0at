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
 * A valid language exit terminates the entire OS process without returning.
 * Otherwise returns host cancellation status or nonzero on uncaught exception or
 * invalid bytecode. Termination bypasses user catch/finally and host destructors.
 * Uncaught values remain in the originating thread's exception field; other diagnostics are
 * incomplete.
 */
int run(process_t *proc, bytecode_t *code);

/** @brief Execute one prepared module file through its once-only initialization gate.
 * The process retains the persistent scope and original failure. Included files
 * execute synchronously using fixed local links and never reopen source paths.
 * The caller frame, instruction pointer and operands survive this outer VM entry.
 * On failure the originating exception is stored in the main thread, as for run.
 * A matching frozen direct-launch entry is not a restricted initializer; nested
 * includes/imports are. Valid explicit exit terminates the OS process without returning.
 * Not a reentrant VM entry: invoke only when no run is active in this process.
 */
int run_module_file(process_t *proc, module_file_t *file);

/** @brief Starts an equal program thread invoking callable with retained arguments.
 * Called with the process execution lock held during an active run. The returned
 * integer is a process-local handle; failures are ordinary catchable exceptions.
 */
operation_result_t spawn_program_thread(thread_t *caller,
                                        object_t *callable,
                                        object_t *const *arguments,
                                        uint16_t count);
/** @brief Waits for a program thread; returns its result or its original exception.
 * Self/cyclic waits and waits during dynamic initialization are rejected.
 */
operation_result_t join_program_thread(thread_t *caller, uint64_t id);
