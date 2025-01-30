/**
 * @file launcher.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Function prototypes and structure definitions for launching the compiler and virtual
 *  machine.
 *
 * This header file defines the functions and structures used to launch the compiler and virtual
 * machine, handling the initialization, execution, and cleanup of both components. It includes
 * the necessary interfaces to start compilation and execution of bytecode.
 */

#pragma once

#include "options.h"

/**
 * @brief Executes the compiler and/or virtual machine based on the provided options.
 * 
 * This function interprets the options provided in the `options_t` structure and launches either
 * the compiler or the virtual machine (or both) based on the parsed command-line arguments.
 * The function returns an exit code that represents the outcome of the execution.
 * 
 * The specific behavior depends on the options set in the `options_t` structure, and additional 
 * options can be added in the future for more flexible control over the execution.
 * 
 * @param opt A pointer to the `options_t` structure that contains the parsed command-line options.
 * @return An integer representing the exit code of the execution.
 *         - `0` if successful.
 *         - A non-zero value if an error occurred (e.g., invalid input file or execution failure).
 */
int go(options_t *opt);
