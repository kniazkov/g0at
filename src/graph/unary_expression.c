/** @file unary_expression.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Unary plus and minus AST nodes.
 */
#include "unary_expression.h"

#include "analysis/reachability.h"
#include "analysis/unary_operation.h"
#include "codegen/code_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"

/** @brief Implements node_vtbl_t::get_child_count. */
static size_t get_child_count(const node_t *node) {
    return 1;
}

/** @brief Implements node_vtbl_t::get_child. */
static node_t *get_child(const node_t *node, size_t index) {
    return index == 0 ? &((const unary_expression_t *)node)->operand->base : NULL;
}

/** @brief Implements node_vtbl_t::get_child_tag. */
static const wchar_t *get_child_tag(const node_t *node, size_t index) {
    return index == 0 ? L"operand" : NULL;
}

/** @brief Implements node_vtbl_t::calculate. */
static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    const lattice_element_t *value =
        calculate_expression(((unary_expression_t *)node)->operand, state, arena);
    return lattice_unary(arena, value, node->vtbl->type == NODE_UNARY_MINUS);
}

/** @brief Implements node_vtbl_t::generate_goat_code, preserving operand grouping. */
static string_value_t generate_goat_code(const node_t *node) {
    string_value_t operand =
        generate_goat_code_from_expression(((const unary_expression_t *)node)->operand);
    string_value_t result = format_string(L"(%s(%s))",
                                          node->vtbl->type == NODE_UNARY_MINUS ? L"-" : L"+",
                                          operand.data);
    FREE_STRING(operand);
    return result;
}

/** @brief Implements node_vtbl_t::generate_indented_goat_code. */
static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    if (node->vtbl->type == NODE_UNARY_MINUS)
        append_static_source(builder, L"(-(");
    else
        append_static_source(builder, L"(+(");
    generate_indented_goat_code_from_expression(((const unary_expression_t *)node)->operand,
                                                builder,
                                                indent);
    append_static_source(builder, L"))");
}

/** @brief Implements node_vtbl_t::generate_bytecode. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    instr_index_t first =
        generate_bytecode_from_expression(((unary_expression_t *)node)->operand, code, data);
    add_instruction(
        code,
        (instruction_t){.opcode = node->vtbl->type == NODE_UNARY_MINUS ? UMINUS : UPLUS});
    return first;
}

/** @brief Implements node_vtbl_t::analyze_reachability. */
static const lattice_element_t *
analyze_reachability(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    const lattice_element_t *value =
        visit_reachable_node(get_node_child(node, 0), state, collector);
    const lattice_element_t *result =
        lattice_unary((*state)->arena, value, node->vtbl->type == NODE_UNARY_MINUS);
    if (result->type == LATTICE_BOTTOM)
        (*state)->control_flow = FLOW_UNREACHABLE;
    return result;
}

/** @brief Virtual table for unary sign operations. */
static node_vtbl_t unary_plus_vtbl = {
    .type = NODE_UNARY_PLUS,
    .analyze_reachability = analyze_reachability,
    .is_pure = children_are_pure,
    .type_name = L"unary plus",
    .get_data = no_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = get_child_count,
    .get_child = get_child,
    .get_child_tag = get_child_tag,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = calculate,
    .execute = execute_nothing,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
    .can_generate_c_code = child_c_code,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = no_indented_c_code,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

/** @brief Virtual table for unary sign operations. */
static node_vtbl_t unary_minus_vtbl = {
    .type = NODE_UNARY_MINUS,
    .analyze_reachability = analyze_reachability,
    .is_pure = children_are_pure,
    .type_name = L"unary minus",
    .get_data = no_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = get_child_count,
    .get_child = get_child,
    .get_child_tag = get_child_tag,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = calculate,
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

static expression_t *create_unary_node(arena_t *arena, expression_t *operand, node_vtbl_t *vtbl) {
    unary_expression_t *node = alloc_zeroed_from_arena(arena, sizeof(*node));
    node->base.base.vtbl = vtbl;
    node->operand = operand;
    return &node->base;
}

expression_t *create_unary_plus_node(arena_t *arena, expression_t *operand) {
    return create_unary_node(arena, operand, &unary_plus_vtbl);
}

expression_t *create_unary_minus_node(arena_t *arena, expression_t *operand) {
    return create_unary_node(arena, operand, &unary_minus_vtbl);
}
