/**
 * @file static_string.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of the static string expression.
 * 
 * This file defines the behavior of the static string expression, which represents a literal
 * string value in the syntax tree. Static strings are immutable and directly correspond to
 * string literals in the source code.
 */

#include <memory.h>

#include "expression.h"
#include "common_methods.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"

/**
 * @struct static_string_t
 * @brief Represents a static string expression node.
 * 
 * This structure defines a static string expression in the syntax tree. A static string
 * is a literal value that appears in the source code and is immutable. The structure
 * extends `expression_t` and includes additional fields for storing the string's value
 * and its length.
 */
typedef struct {
    /**
     * @brief Base expression structure from which static_string_t inherits.
     */
    expression_t base;

    /**
     * @brief Pointer to the wide-character string data.
     */
    wchar_t *data;

    /**
     * @brief Length of the string in characters (not including the null terminator).
     */
    size_t length;
} static_string_t;

/**
 * @brief Converts a static string expression to its string representation.
 * 
 * This function converts the given static string expression to its representation as it would
 * appear in the source code, including surrounding quotes and escaped characters (e.g., `\n`,
 * `\t`, `\\`). The resulting string is generated in a notation suitable for embedding in other
 * contexts, such as code generation.
 * 
 * @param node A pointer to the static string expression node.
 * @return A `string_value_t` containing the formatted string representation.
 */
static string_value_t generate_goat_code(const node_t *node) {
    const static_string_t *expr = (const static_string_t *)node;
    return string_to_string_notation(L"", (string_value_t){ expr->data, expr->length, false });
}

/**
 * @brief Generates bytecode for a static string node.
 * 
 * This function generates bytecode for a static string node by first adding the string
 * to the data segment, and then generating a `SLOAD` instruction with the index of the
 * string in the data segment. 
 * @param node A pointer to the node representing a static string.
 * @param code A pointer to the `code_builder_t` structure used for generating instructions.
 * @param data A pointer to the `data_builder_t` structure used for managing the data segment.
 */
static void generate_bytecode(const node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const static_string_t *expr = (const static_string_t *)node;
    uint32_t index = add_string_to_data_segment_ex(data, expr->data, expr->length);
    add_instruction(code, (instruction_t){ .opcode = SLOAD, .arg1 = index });
}

/**
 * @brief Virtual table for static string expressions.
 * 
 * This virtual table provides the implementation of operations specific to static string
 * expressions. It contains function pointers for operations such as converting the static string
 * to a string representation and generating the corresponding bytecode.
 */
static node_vtbl_t static_string_vtbl = {
    .type = NODE_STATIC_STRING,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = stub_indented_goat_code_generator,
    .generate_bytecode = generate_bytecode,
};

node_t *create_static_string_node(arena_t *arena, const wchar_t *data, size_t length) {
    static_string_t *expr = (static_string_t *)alloc_from_arena(arena, sizeof(static_string_t));
    expr->base.base.vtbl = &static_string_vtbl;
    expr->data = copy_string_to_arena(arena, data, length);
    expr->length = length;
    return &expr->base.base;
}
