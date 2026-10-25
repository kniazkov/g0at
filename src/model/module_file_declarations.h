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

/** Callback receives a borrowed declaration and its DVAR/DCONST instruction. */
typedef bool (*module_declaration_visitor_t)(const bytecode_t *code,
                                             size_t instruction,
                                             const declaration_descriptor_t *declaration,
                                             void *context);
/** Reuse the scope walker without creating runtime objects. Syntactic mode also
 * follows lexical continuations after abrupt transfers, preserving declarations
 * after throw/return; END is the file-code boundary. Disconnected padding and
 * function bodies do not establish file declarations. Inputs must be decoded.
 */
bool visit_module_file_declarations(const bytecode_t *code,
                                    uint32_t file,
                                    bool syntactic,
                                    module_declaration_visitor_t visitor,
                                    void *context);
