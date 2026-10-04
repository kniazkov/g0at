/** @file comparison.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Comparison nodes, evaluation and code generation.
 */
#include "comparison.h"

#include "analysis/comparison.h"
#include "analysis/reachability.h"
#include "analysis/simplification.h"
#include "codegen/c_control.h"
#include "codegen/code_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"

#include <assert.h>

comparison_kind_t node_comparison_kind(node_type_t type) {
    switch (type) {
        case NODE_LESS:
            return COMPARE_LESS;
        case NODE_LESS_OR_EQUAL:
            return COMPARE_LEQ;
        case NODE_GREATER:
            return COMPARE_GREATER;
        case NODE_GREATER_OR_EQUAL:
            return COMPARE_GREQ;
        case NODE_EQUAL:
            return COMPARE_EQUAL;
        case NODE_NOT_EQUAL:
            return COMPARE_NOT_EQUAL;
        default:
            assert(false);
            return COMPARE_EQUAL;
    }
}

static const wchar_t *symbols[] = {L"<", L"<=", L">", L">=", L"==", L"!="};
static const opcode_t opcodes[] = {LESS, LEQ, GREATER, GREQ, EQUAL, DIFF};

/** @brief Numeric operations use the wrap, rounding and comparison rules of the C contract. */
static bool can_generate_c_code(const node_t *node,
                                const lattice_element_t *value,
                                const c_expression_context_t *context) {
    return c_numeric_operands(node, context);
}

/** @brief Implements node_vtbl_t::calculate. */
static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    binary_operation_t *expr = (binary_operation_t *)node;
    const lattice_element_t *left = calculate_expression(expr->left_operand, state, arena);
    const lattice_element_t *right = calculate_expression(expr->right_operand, state, arena);
    return lattice_compare(left, right, node_comparison_kind(node->vtbl->type));
}

/** @brief Implements node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    const binary_operation_t *expr = (const binary_operation_t *)node;
    string_value_t left = generate_goat_code_from_expression(expr->left_operand);
    string_value_t right = generate_goat_code_from_expression(expr->right_operand);
    string_value_t result = format_string(L"(%s %s %s)",
                                          left.data,
                                          symbols[node_comparison_kind(node->vtbl->type)],
                                          right.data);
    FREE_STRING(left);
    FREE_STRING(right);
    return result;
}

/** @brief Implements node_vtbl_t::generate_indented_goat_code. */
static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    append_formatted_source(builder, generate_goat_code(node));
}

/** @brief Implements node_vtbl_t::analyze_reachability. */
static const lattice_element_t *
analyze_reachability(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    const lattice_element_t *left = visit_reachable_node(get_node_child(node, 0), state, collector);
    const lattice_element_t *right =
        visit_reachable_node(get_node_child(node, 1), state, collector);
    const lattice_element_t *result =
        lattice_compare(left, right, node_comparison_kind(node->vtbl->type));
    if (result->type == LATTICE_BOTTOM)
        (*state)->control_flow = FLOW_UNREACHABLE;
    return result;
}

/** @brief Implements node_vtbl_t::generate_bytecode. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    binary_operation_t *expr = (binary_operation_t *)node;
    instr_index_t first = generate_bytecode_from_expression(expr->left_operand, code, data);
    generate_bytecode_from_expression(expr->right_operand, code, data);
    add_instruction(code,
                    (instruction_t){.opcode = opcodes[node_comparison_kind(node->vtbl->type)]});
    return first;
}

static node_vtbl_t vtables[] = {
    {
        .type = NODE_LESS,
        .analyze_reachability = analyze_reachability,
        .is_pure = children_are_pure,
        .simplify = simplify_constant_expression,
        .collect_direct_effects = collect_child_effects,
        .type_name = L"less",
        .get_data = no_data,
        .get_property_count = no_properties,
        .get_property = no_property,
        .get_child_count = binop_get_child_count,
        .get_child = binop_get_child,
        .get_child_tag = binop_get_tag,
        .insert_child_before = no_child_insertion,
        .replace_child = binop_replace_child,
        .get_related_count = no_related_nodes,
        .get_related = no_related_node,
        .get_relation_type = no_relation_type,
        .calculate = calculate,
        .execute = execute_nothing,
        .generate_goat_code = generate_goat_code,
        .generate_indented_goat_code = generate_indented_goat_code,
        .generate_bytecode = generate_bytecode,
        .can_generate_c_code = can_generate_c_code,
        .generate_c_code = c_comparison,
        .generate_indented_c_code = no_indented_c_code,
        .generate_bytecode_assign = no_bytecode_assignment,
        .generate_bytecode_deferred = no_deferred_bytecode,
    },
    {
        .type = NODE_LESS_OR_EQUAL,
        .analyze_reachability = analyze_reachability,
        .is_pure = children_are_pure,
        .simplify = simplify_constant_expression,
        .collect_direct_effects = collect_child_effects,
        .type_name = L"less or equal",
        .get_data = no_data,
        .get_property_count = no_properties,
        .get_property = no_property,
        .get_child_count = binop_get_child_count,
        .get_child = binop_get_child,
        .get_child_tag = binop_get_tag,
        .insert_child_before = no_child_insertion,
        .replace_child = binop_replace_child,
        .get_related_count = no_related_nodes,
        .get_related = no_related_node,
        .get_relation_type = no_relation_type,
        .calculate = calculate,
        .execute = execute_nothing,
        .generate_goat_code = generate_goat_code,
        .generate_indented_goat_code = generate_indented_goat_code,
        .generate_bytecode = generate_bytecode,
        .can_generate_c_code = can_generate_c_code,
        .generate_c_code = c_comparison,
        .generate_indented_c_code = no_indented_c_code,
        .generate_bytecode_assign = no_bytecode_assignment,
        .generate_bytecode_deferred = no_deferred_bytecode,
    },
    {
        .type = NODE_GREATER,
        .analyze_reachability = analyze_reachability,
        .is_pure = children_are_pure,
        .simplify = simplify_constant_expression,
        .collect_direct_effects = collect_child_effects,
        .type_name = L"greater",
        .get_data = no_data,
        .get_property_count = no_properties,
        .get_property = no_property,
        .get_child_count = binop_get_child_count,
        .get_child = binop_get_child,
        .get_child_tag = binop_get_tag,
        .insert_child_before = no_child_insertion,
        .replace_child = binop_replace_child,
        .get_related_count = no_related_nodes,
        .get_related = no_related_node,
        .get_relation_type = no_relation_type,
        .calculate = calculate,
        .execute = execute_nothing,
        .generate_goat_code = generate_goat_code,
        .generate_indented_goat_code = generate_indented_goat_code,
        .generate_bytecode = generate_bytecode,
        .can_generate_c_code = can_generate_c_code,
        .generate_c_code = c_comparison,
        .generate_indented_c_code = no_indented_c_code,
        .generate_bytecode_assign = no_bytecode_assignment,
        .generate_bytecode_deferred = no_deferred_bytecode,
    },
    {
        .type = NODE_GREATER_OR_EQUAL,
        .analyze_reachability = analyze_reachability,
        .is_pure = children_are_pure,
        .simplify = simplify_constant_expression,
        .collect_direct_effects = collect_child_effects,
        .type_name = L"greater or equal",
        .get_data = no_data,
        .get_property_count = no_properties,
        .get_property = no_property,
        .get_child_count = binop_get_child_count,
        .get_child = binop_get_child,
        .get_child_tag = binop_get_tag,
        .insert_child_before = no_child_insertion,
        .replace_child = binop_replace_child,
        .get_related_count = no_related_nodes,
        .get_related = no_related_node,
        .get_relation_type = no_relation_type,
        .calculate = calculate,
        .execute = execute_nothing,
        .generate_goat_code = generate_goat_code,
        .generate_indented_goat_code = generate_indented_goat_code,
        .generate_bytecode = generate_bytecode,
        .can_generate_c_code = can_generate_c_code,
        .generate_c_code = c_comparison,
        .generate_indented_c_code = no_indented_c_code,
        .generate_bytecode_assign = no_bytecode_assignment,
        .generate_bytecode_deferred = no_deferred_bytecode,
    },
    {
        .type = NODE_EQUAL,
        .analyze_reachability = analyze_reachability,
        .is_pure = children_are_pure,
        .simplify = simplify_constant_expression,
        .collect_direct_effects = collect_child_effects,
        .type_name = L"equal",
        .get_data = no_data,
        .get_property_count = no_properties,
        .get_property = no_property,
        .get_child_count = binop_get_child_count,
        .get_child = binop_get_child,
        .get_child_tag = binop_get_tag,
        .insert_child_before = no_child_insertion,
        .replace_child = binop_replace_child,
        .get_related_count = no_related_nodes,
        .get_related = no_related_node,
        .get_relation_type = no_relation_type,
        .calculate = calculate,
        .execute = execute_nothing,
        .generate_goat_code = generate_goat_code,
        .generate_indented_goat_code = generate_indented_goat_code,
        .generate_bytecode = generate_bytecode,
        .can_generate_c_code = can_generate_c_code,
        .generate_c_code = c_comparison,
        .generate_indented_c_code = no_indented_c_code,
        .generate_bytecode_assign = no_bytecode_assignment,
        .generate_bytecode_deferred = no_deferred_bytecode,
    },
    {
        .type = NODE_NOT_EQUAL,
        .analyze_reachability = analyze_reachability,
        .is_pure = children_are_pure,
        .simplify = simplify_constant_expression,
        .collect_direct_effects = collect_child_effects,
        .type_name = L"not equal",
        .get_data = no_data,
        .get_property_count = no_properties,
        .get_property = no_property,
        .get_child_count = binop_get_child_count,
        .get_child = binop_get_child,
        .get_child_tag = binop_get_tag,
        .insert_child_before = no_child_insertion,
        .replace_child = binop_replace_child,
        .get_related_count = no_related_nodes,
        .get_related = no_related_node,
        .get_relation_type = no_relation_type,
        .calculate = calculate,
        .execute = execute_nothing,
        .generate_goat_code = generate_goat_code,
        .generate_indented_goat_code = generate_indented_goat_code,
        .generate_bytecode = generate_bytecode,
        .can_generate_c_code = can_generate_c_code,
        .generate_c_code = c_comparison,
        .generate_indented_c_code = no_indented_c_code,
        .generate_bytecode_assign = no_bytecode_assignment,
        .generate_bytecode_deferred = no_deferred_bytecode,
    },
};

expression_t *create_comparison_node(arena_t *arena,
                                     comparison_kind_t kind,
                                     expression_t *left,
                                     expression_t *right) {
    binary_operation_t *expr = alloc_zeroed_from_arena(arena, sizeof(*expr));
    expr->base.base.vtbl = &vtables[kind];
    expr->left_operand = left;
    expr->right_operand = right;
    return &expr->base;
}

expression_t *create_less_or_equal_node(arena_t *arena, expression_t *left, expression_t *right) {
    return create_comparison_node(arena, COMPARE_LEQ, left, right);
}

expression_t *
create_greater_or_equal_node(arena_t *arena, expression_t *left, expression_t *right) {
    return create_comparison_node(arena, COMPARE_GREQ, left, right);
}

expression_t *create_equal_node(arena_t *arena, expression_t *left, expression_t *right) {
    return create_comparison_node(arena, COMPARE_EQUAL, left, right);
}

expression_t *create_not_equal_node(arena_t *arena, expression_t *left, expression_t *right) {
    return create_comparison_node(arena, COMPARE_NOT_EQUAL, left, right);
}
