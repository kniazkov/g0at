/**
 * @file integer.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of the integer literal expression.
 * 
 * This file defines the behavior of the integer literal expression, which represents
 * a signed 64-bit integer value in the syntax tree. Integer literals are immutable
 * and directly correspond to numeric constants in the source code.
 */

#include "expression.h"
#include "common_methods.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "lib/split64.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"

/**
 * @struct integer_t
 * @brief Represents an integer literal expression node.
 * 
 * This structure defines an integer literal expression in the syntax tree.
 * The structure extends `expression_t` and includes a field for storing
 * the 64-bit signed integer value.
 */
typedef struct {
    /**
     * @brief Base expression structure from which integer_t inherits.
     */
    expression_t base;

    /**
     * @brief The 64-bit signed integer value.
     */
    int64_t value;
} integer_t;

/**
 * @brief Converts an integer expression to its string representation.
 * 
 * This function converts the given integer expression to its decimal string
 * representation as it would appear in the source code.
 * 
 * @param node A pointer to the integer expression node.
 * @return A `string_value_t` containing the formatted decimal string.
 */
static string_value_t get_data_and_generate_goat_code(const node_t *node) {
    const integer_t *expr = (const integer_t *)node;
    return format_string(L"%ld", expr->value);
}

/**
 * @brief Generates indented Goat source code for an integer literal expression.
 *
 * This function implements the virtual method for generating Goat source code that represents
 * an integer literal. It converts the integer value to its string representation and outputs
 * it directly without any additional formatting or decorations.

 * @param node Pointer to the AST node representing the integer literal.
 * @param builder Pointer to the source builder where generated code will be stored.
 * @param indent The current indentation level (in tabs) for code generation.
 */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
            size_t indent) {
    const integer_t *expr = (const integer_t *)node;
    append_formatted_source(builder, format_string(L"%ld", expr->value));
}

/**
 * @brief Generates bytecode for an integer node.
 * 
 * This function generates bytecode for an integer node by emitting either:
 * - An `ILOAD32` instruction for values fitting in 32 bits (range [-2^31, 2^31-1])
 * - An `ILOAD64` instruction with `ARG` prefix for 64-bit values
 * 
 * @param node A pointer to the node representing an integer literal.
 * @param code A pointer to the `code_builder_t` structure for instruction generation.
 * @param data Unused parameter (kept for signature compatibility with other generators).
 */
static void generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const integer_t *expr = (const integer_t *)node;
    if (expr->value > INT32_MAX || expr->value < INT32_MIN) {
        split64_t s;
        s.int_value = expr->value;
        add_instruction(code, (instruction_t){ .opcode = ARG, .arg1 = s.parts[0] });
        add_instruction(code, (instruction_t){ .opcode = ILOAD64, .arg1 = s.parts[1] });
    } else {
        add_instruction(code, (instruction_t){ .opcode = ILOAD32, .arg1 = expr->value });
    }
}

/**
 * @brief Virtual table for integer expressions.
 * 
 * This virtual table provides the implementation of operations specific to integer
 * literal expressions, including code generation and string representation.
 */
static node_vtbl_t integer_vtbl = {
    .type = NODE_INTEGER,
    .type_name = L"integer",
    .get_data = get_data_and_generate_goat_code,
    .get_child_count = no_children,
    .get_child = no_child,
    .get_child_tag = no_tags,
    .generate_goat_code = get_data_and_generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
};

node_t *create_integer_node(arena_t *arena, int64_t value) {
    integer_t *expr = (integer_t *)alloc_from_arena(arena, sizeof(integer_t));
    expr->base.base.vtbl = &integer_vtbl;
    expr->value = value;
    return &expr->base.base;
}
