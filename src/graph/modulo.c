/**
 * @file modulo.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of the modulo (remainder) binary operation expression node.
 * 
 * This file defines the behavior of the modulo expression node, which represents the
 * remainder of the division of two operands in the syntax tree. 
 */

#include "binary_operation.h"
#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"

/**
 * @struct modulo_t
 * @brief Represents a modulo operation expression node.
 * 
 * This structure defines a modulo operation in the syntax tree. The modulo node
 * extends `binary_operation_t` and includes references to the left and right operands
 * whose remainder is to be computed.
 */
typedef struct {
    /**
     * @brief Base binary operation structure from which modulo_t inherits.
     */
    binary_operation_t base;
} modulo_t;

/**
 * @brief Converts a modulo operation expression to its string representation.
 * 
 * This function converts the given modulo expression to its representation as it would
 * appear in the source code (e.g., the left operand, the "%" operator, and the right operand).
 * 
 * @param node A pointer to the modulo expression node.
 * @return A `string_value_t` containing the formatted string representation.
 */
static string_value_t generate_goat_code(const node_t *node) {
    const modulo_t *expr = (const modulo_t *)node;
    string_value_t left =
        expr->base.left_operand->base.vtbl->generate_goat_code(&expr->base.left_operand->base);
    string_value_t right =
        expr->base.right_operand->base.vtbl->generate_goat_code(&expr->base.right_operand->base);
    string_value_t result = format_string(L"%s %% %s", left.data, right.data);
    FREE_STRING(left);
    FREE_STRING(right);
    return result;
}

/**
 * @brief Generates indented Goat source code for a modulo operation node.
 * 
 * This function generates code for both operands, combining them with the modulo `%` operator.
 * 
 * @param node Pointer to the modulo node to generate code for.
 * @param builder Pointer to the source builder.
 * @param indent The current indentation level (in tabs).
 */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
            size_t indent) {
    const modulo_t *expr = (const modulo_t *)node;
    expr->base.left_operand->base.vtbl->generate_indented_goat_code(&expr->base.left_operand->base,
        builder, indent);
    append_static_source(builder, L" % ");
    expr->base.right_operand->base.vtbl->generate_indented_goat_code(
        &expr->base.right_operand->base, builder, indent);
}

/**
 * @brief Generates bytecode for a modulo operation node.
 * 
 * Generates code for the operands and then emits a `MODULO` instruction.
 * 
 * @param node A pointer to the node representing the modulo operation.
 * @param code A pointer to the code builder.
 * @param data A pointer to the data builder.
 * @return The instruction index of the first emitted instruction.
 */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const modulo_t *expr = (const modulo_t *)node;
    instr_index_t first = expr->base.left_operand->base.vtbl->generate_bytecode(
        &expr->base.left_operand->base, code, data);
    expr->base.right_operand->base.vtbl->generate_bytecode(
        &expr->base.right_operand->base, code, data);
    add_instruction(code, (instruction_t){ .opcode = MODULO });
    return first;
}

/**
 * @brief Virtual table for modulo operations.
 */
static node_vtbl_t modulo_vtbl = {
    .type = NODE_MODULO,
    .type_name = L"modulo",
    .get_data = no_data,
    .get_child_count = binop_get_child_count,
    .get_child = binop_get_child,
    .get_child_tag = binop_get_tag,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode
};

expression_t *create_modulo_node(arena_t *arena, expression_t *left_operand,
        expression_t *right_operand) {
    modulo_t *expr = (modulo_t *)alloc_from_arena(arena, sizeof(modulo_t));
    expr->base.base.base.vtbl = &modulo_vtbl;
    expr->base.left_operand = left_operand;
    expr->base.right_operand = right_operand;
    return &expr->base.base;
}
