/** @file module_program.h
 * @copyright 2026 Ivan Kniazkov
 * @brief CLI composition of frozen compilation, publication and execution.
 */
#pragma once
#include "options.h"
/** Handle descriptors, module archives and source include/import graphs. Plain
 * legacy source/binaries return handled=false without changing the options. */
int try_module_program(const options_t *options, bool *handled);
