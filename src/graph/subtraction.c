/**
 * @file subtraction.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the subtraction binary operation expression node.
 */

#include "binary_operation.h"
#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"

/** @brief A subtraction operation expression node. */
typedef struct {
    /** @brief Base binary operation structure from which subtraction_t inherits. */
    binary_operation_t base;
} subtraction_t;

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    const subtraction_t *expr = (const subtraction_t *)node;
    string_value_t left = generate_goat_code_from_expression(expr->base.left_operand);
    string_value_t right = generate_goat_code_from_expression(expr->base.right_operand);
    string_value_t result = format_string(L"%s - %s", left.data, right.data);
    FREE_STRING(left);
    FREE_STRING(right);
    return result;
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
            size_t indent) {
    const subtraction_t *expr = (const subtraction_t *)node;
    generate_indented_goat_code_from_expression(expr->base.left_operand, builder, indent);
    append_static_source(builder, L" - ");
    generate_indented_goat_code_from_expression(expr->base.right_operand, builder, indent);
}

/** @brief Generates bytecode for a subtraction operation node. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const subtraction_t *expr = (const subtraction_t *)node;
    instr_index_t first = generate_bytecode_from_expression(expr->base.left_operand, code, data);
    generate_bytecode_from_expression(expr->base.right_operand, code, data);
    add_instruction(code, (instruction_t){ .opcode = SUB });
    return first;
}

/** @brief Virtual table for subtraction operations. */
static node_vtbl_t subtraction_vtbl = {
    .type = NODE_SUBTRACTION,
    .type_name = L"subtraction",
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
    .generate_bytecode = generate_bytecode,
    .can_generate_c_code = cannot_generate_c_code,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = no_indented_c_code,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

expression_t *create_subtraction_node(arena_t *arena, expression_t *left_operand,
        expression_t *right_operand) {
    subtraction_t *expr = (subtraction_t *)alloc_zeroed_from_arena(arena, sizeof(subtraction_t));
    expr->base.base.base.vtbl = &subtraction_vtbl;
    expr->base.left_operand = left_operand;
    expr->base.right_operand = right_operand;
    return &expr->base.base;
}
