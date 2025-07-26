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
#include "codegen/source_builder.h"

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
     * @brief String data.
     */
    string_view_t string;
} static_string_t;

/**
 * @brief Gets the string data of a static string node.
 * 
 * Returns the string value contained in the static string node.
 * The returned string_value_t shares the same memory and doesn't need freeing.
 * 
 * @param node Pointer to the node.
 * @return `string_value_t` containing the string data.
 */
static string_value_t get_data(const node_t *node) {
    const static_string_t *expr = (const static_string_t *)node;
    return VIEW_TO_VALUE(expr->string);
}

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
    return string_to_string_notation(L"", VIEW_TO_VALUE(expr->string));
}

/**
 * @brief Generates indented Goat source code for a static string literal expression.
 * 
 * This function implements the virtual method for generating Goat source code that represents
 * a string literal. It outputs the string in proper Goat syntax, including escaping special
 * characters and wrapping the content in quotation marks.
 *
 * @param node Pointer to the AST node representing the string literal.
 * @param builder Pointer to the source builder where generated code will be stored.
 * @param indent The current indentation level (in tabs) for code generation.
 */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
        size_t indent) {
    const static_string_t *expr = (const static_string_t *)node;
    append_formatted_source(
        builder,
        string_to_string_notation(L"", VIEW_TO_VALUE(expr->string))
    );
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
 * @return The instruction index of the first emitted instruction.
 */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const static_string_t *expr = (const static_string_t *)node;
    uint32_t index = add_string_to_data_segment_ex(data, expr->string);
    return add_instruction(code, (instruction_t){ .opcode = SLOAD, .arg1 = index });
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
    .type_name = L"static string",
    .get_data = get_data,
    .get_child_count = no_children,
    .get_child = no_child,
    .get_child_tag = no_tags,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
};

node_t *create_static_string_node(arena_t *arena, const wchar_t *data, size_t length) {
    static_string_t *expr = (static_string_t *)alloc_from_arena(arena, sizeof(static_string_t));
    expr->base.base.vtbl = &static_string_vtbl;
    expr->string = copy_string_to_arena(arena, data, length);
    return &expr->base.base;
}
