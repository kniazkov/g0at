/**
 * @file vm.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines the virtual machine for the Goat programming language.
 */

#pragma once

#include "bytecode.h"
#include "model/process.h"

/**
 * @brief Runs bytecode in proc; the caller retains both objects.
 * The process is initialized automatically if needed.
 * Returns nonzero on ADD failure; other instruction diagnostics remain incomplete.
 */
int run(process_t *proc, bytecode_t *code);
