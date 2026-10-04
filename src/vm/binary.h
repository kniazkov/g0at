/** @file binary.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Versioned bytecode artifacts and persistent native bindings.
 */
#pragma once
#include "bytecode.h"

#define GOAT_BINARY_LIMIT (256u * 1024u * 1024u)

/** @brief Decoded artifact; release with destroy_binary_program. */
typedef struct {
    bytecode_t *code;
    uint64_t *bindings; /**< Pairs: FUNC instruction index, native function ID. */
    size_t binding_count;
    uint64_t library_checksum;
} binary_program_t;

bool save_binary_program(const char *path, const bytecode_t *code, uint64_t library_checksum);
binary_program_t load_binary_program(const char *path);
/** @brief Validates and attaches a matching trusted library without compiling anything. */
bool bind_binary_library(binary_program_t *program, const char *path);
void destroy_binary_program(binary_program_t *program);
