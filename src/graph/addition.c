/**
 * @file addition.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of the addition binary operation expression node.
 * 
 * This file defines the behavior of the addition expression node, which represents the addition
 * of two operands in the syntax tree. 
 */

#include "binary_operation.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"

/**
 * @struct addition_t
 * @brief Represents an addition operation expression node.
 * 
 * This structure defines an addition operation in the syntax tree. The addition node
 * extends `binary_operation_t` and includes references to the left and right operands
 * to be added together.
 */
typedef struct {
    /**
     * @brief Base binary operation structure from which addition_t inherits.
     */
    binary_operation_t base;
} addition_t;

/**
 * @brief Converts an addition operation expression to its string representation.
 * 
 * This function converts the given addition expression to its representation as it would
 * appear in the source code (e.g., the left operand, the "+" operator, and the right operand).
 * The resulting string is suitable for embedding in other contexts, such as code generation.
 * 
 * @param node A pointer to the addition expression node.
 * @return A `string_value_t` containing the formatted string representation.
 */
static string_value_t addition_to_string(const node_t *node) {
    const addition_t *expr = (const addition_t *)node;
    string_value_t left =
        expr->base.left_operand->base.vtbl->to_string(&expr->base.left_operand->base);
    string_value_t right =
        expr->base.right_operand->base.vtbl->to_string(&expr->base.right_operand->base);
    string_value_t result = format_string(L"%s + %s", left.data, right.data);
    if (left.should_free) {
        FREE(left.data);
    }
    if (right.should_free) {
        FREE(right.data);
    }
    return result;
}

/**
 * @brief Generates bytecode for an addition operation node.
 * 
 * This function generates bytecode for an addition operation by first generating the bytecode
 * for the left and right operands, and then generating the `ADD` instruction for the addition.
 * 
 * @param node A pointer to the node representing the addition operation.
 * @param code A pointer to the `code_builder_t` structure used for generating instructions.
 * @param data A pointer to the `data_builder_t` structure used for managing the data segment.
 */
static void gen_bytecode_for_addition(const node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const addition_t *expr = (const addition_t *)node;
    expr->base.left_operand->base.vtbl->gen_bytecode(&expr->base.left_operand->base, code, data);
    expr->base.right_operand->base.vtbl->gen_bytecode(&expr->base.right_operand->base, code, data);
    add_instruction(code, (instruction_t){ .opcode = ADD });
}

/**
 * @brief Virtual table for addition operations.
 * 
 * This virtual table provides the implementation of operations specific to addition expressions.
 * It includes function pointers for operations such as converting the addition expression to
 * a string representation and generating the corresponding bytecode.
 * 
 * The table includes the following function pointers:
 * - `to_string`: Converts the addition expression node to a string representation.
 * - `gen_bytecode`: Generates the bytecode for the addition expression, using `ADD`.
 */
static node_vtbl_t addition_vtbl = {
    .type = NODE_ADDITION,
    .to_string = addition_to_string,
    .gen_bytecode = gen_bytecode_for_addition
};

expression_t *create_addition_node(arena_t *arena, expression_t *left_operand,
        expression_t *right_operand) {
    addition_t *expr = (addition_t *)alloc_from_arena(arena, sizeof(addition_t));
    expr->base.base.base.vtbl = &addition_vtbl;
    expr->base.left_operand = left_operand;
    expr->base.right_operand = right_operand;
    return &expr->base.base;
}
