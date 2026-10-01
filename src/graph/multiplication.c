/**
 * @file multiplication.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the multiplication binary operation expression node.
 */

#include "binary_operation.h"
#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"

/** @brief A multiplication operation expression node. */
typedef struct {
    /** @brief Base binary operation structure from which multiplication_t inherits. */
    binary_operation_t base;
} multiplication_t;

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    const multiplication_t *expr = (const multiplication_t *)node;
    string_value_t left = generate_goat_code_from_expression(expr->base.left_operand);
    string_value_t right = generate_goat_code_from_expression(expr->base.right_operand);
    string_value_t result = format_string(L"%s * %s", left.data, right.data);
    FREE_STRING(left);
    FREE_STRING(right);
    return result;
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
            size_t indent) {
    const multiplication_t *expr = (const multiplication_t *)node;
    generate_indented_goat_code_from_expression(expr->base.left_operand, builder, indent);
    append_static_source(builder, L" * ");
    generate_indented_goat_code_from_expression(expr->base.right_operand, builder, indent);
}

/** @brief Generates bytecode for a multiplication operation node. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const multiplication_t *expr = (const multiplication_t *)node;
    instr_index_t first = generate_bytecode_from_expression(expr->base.left_operand, code, data);
    generate_bytecode_from_expression(expr->base.right_operand, code, data);
    add_instruction(code, (instruction_t){ .opcode = MUL });
    return first;
}

/** @brief Virtual table for multiplication operations. */
static node_vtbl_t multiplication_vtbl = {
    .type = NODE_MULTIPLICATION,
    .type_name = L"multiplication",
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

expression_t *create_multiplication_node(arena_t *arena, expression_t *left_operand,
        expression_t *right_operand) {
    multiplication_t *expr = (multiplication_t *)alloc_zeroed_from_arena(arena, sizeof(multiplication_t));
    expr->base.base.base.vtbl = &multiplication_vtbl;
    expr->base.left_operand = left_operand;
    expr->base.right_operand = right_operand;
    return &expr->base.base;
}
