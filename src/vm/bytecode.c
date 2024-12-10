/**
 * @file bytecode.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implements functions for managing bytecode.
 */

#include "bytecode.h"
#include "lib/allocate.h"

void free_bytecode(bytecode_t *code) {
    FREE(code->buffer);
    FREE(code);
}
