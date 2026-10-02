/** @file update_expression.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Prefix and postfix numeric updates.
 */
#include "update_expression.h"

#include "analysis/update.h"
#include "codegen/code_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "variable.h"

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
    unary_expression_t *expr = (unary_expression_t *)node;
    const lattice_element_t *old = calculate_expression(expr->operand, state, arena);
    const lattice_element_t *value =
        lattice_update(arena, old, update_is_decrement(node->vtbl->type));
    variable_t *var = (variable_t *)expr->operand;
    if (var->declarator && var->declarator != get_builtin_declarator()
        && var->declarator->base.vtbl->type == NODE_CONSTANT_DECLARATOR)
        return make_bottom_element();
    if (state->control_flow != FLOW_NORMAL || value->type == LATTICE_BOTTOM)
        return make_bottom_element();
    if (var->declarator != get_builtin_declarator())
        set_in_abstract_state_at(state, var->declarator, value, node);
    return update_is_postfix(node->vtbl->type) ? old : value;
}

/** @brief Implements node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    string_value_t operand =
        generate_goat_code_from_expression(((const unary_expression_t *)node)->operand);
    const wchar_t *op = update_is_decrement(node->vtbl->type) ? L"--" : L"++";
    string_value_t result = update_is_postfix(node->vtbl->type)
                                ? format_string(L"(%s%s)", operand.data, op)
                                : format_string(L"(%s%s)", op, operand.data);
    FREE_STRING(operand);
    return result;
}

/** @brief Implements node_vtbl_t::generate_indented_goat_code. */
static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    append_formatted_source(builder, generate_goat_code(node));
}

/** @brief Implements node_vtbl_t::generate_bytecode. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    expression_t *operand = ((unary_expression_t *)node)->operand;
    instr_index_t first = generate_bytecode_from_expression(operand, code, data);
    bool postfix = update_is_postfix(node->vtbl->type);
    if (postfix)
        add_instruction(code, (instruction_t){.opcode = DUP});
    add_instruction(code,
                    (instruction_t){.opcode = update_is_decrement(node->vtbl->type) ? DEC : INC});
    generate_bytecode_assign_from_node(&operand->base, code, data);
    if (postfix)
        add_instruction(code, (instruction_t){.opcode = POP});
    return first;
}

/** @brief Virtual table for update expressions. */
static node_vtbl_t prefix_increment_vtbl = {
    .type = NODE_PREFIX_INCREMENT,
    .type_name = L"prefix increment",
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

/** @brief Virtual table for update expressions. */
static node_vtbl_t prefix_decrement_vtbl = {
    .type = NODE_PREFIX_DECREMENT,
    .type_name = L"prefix decrement",
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

/** @brief Virtual table for update expressions. */
static node_vtbl_t postfix_increment_vtbl = {
    .type = NODE_POSTFIX_INCREMENT,
    .type_name = L"postfix increment",
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

/** @brief Virtual table for update expressions. */
static node_vtbl_t postfix_decrement_vtbl = {
    .type = NODE_POSTFIX_DECREMENT,
    .type_name = L"postfix decrement",
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

expression_t *create_prefix_increment_node(arena_t *arena, assignable_expression_t *operand) {
    unary_expression_t *node = alloc_zeroed_from_arena(arena, sizeof(*node));
    node->base.base.vtbl = &prefix_increment_vtbl;
    node->operand = &operand->base;
    return &node->base;
}

expression_t *create_prefix_decrement_node(arena_t *arena, assignable_expression_t *operand) {
    unary_expression_t *node = alloc_zeroed_from_arena(arena, sizeof(*node));
    node->base.base.vtbl = &prefix_decrement_vtbl;
    node->operand = &operand->base;
    return &node->base;
}

expression_t *create_postfix_increment_node(arena_t *arena, assignable_expression_t *operand) {
    unary_expression_t *node = alloc_zeroed_from_arena(arena, sizeof(*node));
    node->base.base.vtbl = &postfix_increment_vtbl;
    node->operand = &operand->base;
    return &node->base;
}

expression_t *create_postfix_decrement_node(arena_t *arena, assignable_expression_t *operand) {
    unary_expression_t *node = alloc_zeroed_from_arena(arena, sizeof(*node));
    node->base.base.vtbl = &postfix_decrement_vtbl;
    node->operand = &operand->base;
    return &node->base;
}
