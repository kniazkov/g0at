/**
 * @file greater.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the greater-than binary operation expression node.
 * 
 * This file defines the behavior of the greater-than expression node, which represents the
 * comparison of two operands in the syntax tree. The node evaluates to a boolean value
 * indicating whether the left operand is strictly greater than the right operand.
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
 * @struct greater_t
 * @brief Represents a greater-than (`>`) operation expression node.
 * 
 * This structure defines a greater-than comparison in the syntax tree. The node extends
 * `binary_operation_t` and includes references to the left and right operands to be compared.
 */
typedef struct {
    /**
     * @brief Base binary operation structure from which greater_t inherits.
     */
    binary_operation_t base;
} greater_t;

/**
 * @brief Converts a greater-than operation expression to its string representation.
 * 
 * This function converts the given greater-than expression to how it would appear
 * in source code (i.e., the left operand, the ">" operator, and the right operand).
 * The resulting string is suitable for embedding in other contexts, such as code generation.
 * 
 * @param node A pointer to the greater-than expression node.
 * @return A `string_value_t` containing the formatted string representation.
 */
static string_value_t generate_goat_code(const node_t *node) {
    const greater_t *expr = (const greater_t *)node;
    string_value_t left =
        generate_goat_code_from_expression(expr->base.left_operand);
    string_value_t right =
        generate_goat_code_from_expression(expr->base.right_operand);
    string_value_t result = format_string(L"%s > %s", left.data, right.data);
    FREE_STRING(left);
    FREE_STRING(right);
    return result;
}

/**
 * @brief Generates indented Goat source code for a greater-than operation node.
 * 
 * This function implements the virtual method for generating Goat source code for a
 * greater-than expression (`>` operator). It recursively generates code for both left and
 * right operands, combining them with the greater-than operator in between.
 * 
 * @param node Pointer to the greater-than node to generate code for.
 * @param builder Pointer to the source builder where generated code will be stored.
 * @param indent The current indentation level (in tabs) for code generation.
 */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
            size_t indent) {
    const greater_t *expr = (const greater_t *)node;
    generate_indented_goat_code_from_expression(expr->base.left_operand, builder, indent);
    append_static_source(builder, L" > ");
    generate_indented_goat_code_from_expression(expr->base.right_operand, builder, indent);
}

/**
 * @brief Generates bytecode for a greater-than operation node.
 * 
 * This function generates bytecode for a greater-than comparison by first generating the bytecode
 * for the left and right operands, and then emitting the `GREATER` instruction for the comparison.
 * 
 * @param node A pointer to the node representing the greater-than operation.
 * @param code A pointer to the `code_builder_t` structure used for generating instructions.
 * @param data A pointer to the `data_builder_t` structure used for managing the data segment.
 * @return The instruction index of the first emitted instruction.
 */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const greater_t *expr = (const greater_t *)node;
    instr_index_t first = generate_bytecode_from_expression(expr->base.left_operand, code, data);
    generate_bytecode_from_expression(expr->base.right_operand, code, data);
    add_instruction(code, (instruction_t){ .opcode = GREATER });
    return first;
}

/**
 * @brief Virtual table for greater-than operations.
 * 
 * This virtual table provides the implementation of operations specific to greater-than expressions.
 * It includes function pointers for operations such as converting the expression to a string
 * representation and generating the corresponding bytecode.
 */
static node_vtbl_t greater_vtbl = {
    .type = NODE_GREATER,
    .type_name = L"greater",
    .get_data = no_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = binop_get_child_count,
    .get_child = binop_get_child,
    .get_child_tag = binop_get_tag,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = cannot_calculate,
    .execute = execute_nothing,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode
};

expression_t *create_greater_node(arena_t *arena, expression_t *left_operand,
        expression_t *right_operand) {
    greater_t *expr = (greater_t *)alloc_zeroed_from_arena(arena, sizeof(greater_t));
    expr->base.base.base.vtbl = &greater_vtbl;
    expr->base.left_operand = left_operand;
    expr->base.right_operand = right_operand;
    return &expr->base.base;
}
