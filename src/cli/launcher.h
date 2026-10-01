/**
 * @file launcher.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Function prototypes and structure definitions for launching the compiler and virtual
 * machine.
 */

#pragma once

#include "options.h"

/**
 * @brief Executes the compiler and/or virtual machine based on the provided options.
 * @return An integer representing the exit code of the execution. - `0` if successful. - A non-zero
 * value if an error occurred (e.g., invalid input file or execution failure).
 */
int go(options_t *opt);
