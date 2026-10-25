/** @file module_file_declarations.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Reserve persistent file declarations without executing their initializers.
 */
#pragma once
#include "module_files.h"

/** Analyze file-level control flow and reserve its declarations once before execution.
 * Function bodies and nested scopes are not file declarations. Invalid scope joins or
 * duplicate names return false; valid bytecode is never changed. Core OOM is fatal.
 */
bool prepare_module_file_declarations(module_file_t *file);
