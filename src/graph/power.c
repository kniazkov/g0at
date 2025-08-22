/**
 * @file power.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of the power binary operation expression node.
 * 
 * This file defines the behavior of the power expression node, which represents the
 * exponentiation of two operands in the syntax tree. 
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
 * @struct power_t
 * @brief Represents a power operation expression node.
 * 
 * This structure defines a power operation in the syntax tree. The power node
 * extends `binary_operation_t` and includes references to the left and right operands
 * (base and exponent).
 */
typedef struct {
    /**
     * @brief Base binary operation structure from which power_t inherits.
     */
    binary_operation_t base;
} power_t;

/**
 * @brief Converts a power operation expression to its string representation.
 * 
 * This function converts the given power expression to its representation as it would
 * appear in the source code (e.g., the left operand, the "**" operator, and the right operand).
 * The resulting string is suitable for embedding in other contexts, such as code generation.
 * 
 * @param node A pointer to the power expression node.
 * @return A `string_value_t` containing the formatted string representation.
 */
static string_value_t generate_goat_code(const node_t *node) {
    const power_t *expr = (const power_t *)node;
    string_value_t left =
        expr->base.left_operand->base.vtbl->generate_goat_code(&expr->base.left_operand->base);
    string_value_t right =
        expr->base.right_operand->base.vtbl->generate_goat_code(&expr->base.right_operand->base);
    string_value_t result = format_string(L"%s ** %s", left.data, right.data);
    FREE_STRING(left);
    FREE_STRING(right);
    return result;
}

/**
 * @brief Generates indented Goat source code for a power operation node.
 * 
 * This function implements the virtual method for generating Goat source code for a power
 * expression (`**` operator). It recursively generates code for both left and right operands,
 * combining them with the power operator in between.
 * 
 * @param node Pointer to the power node to generate code for.
 * @param builder Pointer to the source builder where generated code will be stored.
 * @param indent The current indentation level (in tabs) for code generation.
 */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
            size_t indent) {
    const power_t *expr = (const power_t *)node;
    expr->base.left_operand->base.vtbl->generate_indented_goat_code(&expr->base.left_operand->base,
        builder, indent);
    append_static_source(builder, L" ** ");
    expr->base.right_operand->base.vtbl->generate_indented_goat_code(
        &expr->base.right_operand->base, builder, indent);
}

/**
 * @brief Generates bytecode for a power operation node.
 * 
 * This function generates bytecode for a power operation by first generating the bytecode
 * for the left and right operands, and then generating the `POWER` instruction.
 * 
 * @param node A pointer to the node representing the power operation.
 * @param code A pointer to the `code_builder_t` structure used for generating instructions.
 * @param data A pointer to the `data_builder_t` structure used for managing the data segment.
 * @return The instruction index of the first emitted instruction.
 */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const power_t *expr = (const power_t *)node;
    instr_index_t first = expr->base.left_operand->base.vtbl->generate_bytecode(
        &expr->base.left_operand->base, code, data);
    expr->base.right_operand->base.vtbl->generate_bytecode(
        &expr->base.right_operand->base, code, data);
    add_instruction(code, (instruction_t){ .opcode = POWER });
    return first;
}

/**
 * @brief Virtual table for power operations.
 * 
 * This virtual table provides the implementation of operations specific to power
 * expressions. It includes function pointers for operations such as converting the power
 * expression to a string representation and generating the corresponding bytecode.
 */
static node_vtbl_t power_vtbl = {
    .type = NODE_POWER,
    .type_name = L"power",
    .get_data = no_data,
    .get_child_count = binop_get_child_count,
    .get_child = binop_get_child,
    .get_child_tag = binop_get_tag,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode
};

expression_t *create_power_node(arena_t *arena, expression_t *left_operand,
        expression_t *right_operand) {
    power_t *expr = (power_t *)alloc_from_arena(arena, sizeof(power_t));
    expr->base.base.base.vtbl = &power_vtbl;
    expr->base.left_operand = left_operand;
    expr->base.right_operand = right_operand;
    return &expr->base.base;
}
