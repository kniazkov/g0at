/**
 * @file variable.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the variable expression node.
 */

#include "variable.h"

#include "analysis/abstract_state.h"
#include "analysis/c_body.h"
#include "analysis/function_effects.h"
#include "analysis/lattice.h"
#include "analysis/reachability.h"
#include "builtins/registry.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "declarations.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "statement.h"

#include <assert.h>

/** @brief Implements @ref node_vtbl_t::get_data. */
static node_display_value_t get_data(const node_t *node) {
    const variable_t *expr = (const variable_t *)node;
    node_display_value_kind_t kind = NODE_DISPLAY_VALUE_PLAIN;
    if (expr->declarator && expr->declarator->name.length > 0
        && expr->declarator->name.data[0] == L'*') {
        kind = NODE_DISPLAY_VALUE_PREDEFINED;
    }
    return (node_display_value_t){.text = VIEW_TO_VALUE(expr->name), .kind = kind};
}

/** @brief Implements @ref node_vtbl_t::get_related_count. */
static size_t get_related_count(const node_t *node) {
    const variable_t *expr = (const variable_t *)node;
    return expr->declarator ? 1 : 0;
}

/** @brief Implements @ref node_vtbl_t::get_related. */
static const node_t *get_related(const node_t *node, size_t index) {
    const variable_t *expr = (const variable_t *)node;
    if (index == 0) {
        return &expr->declarator->base;
    }
    return NULL;
}

/** @brief Implements @ref node_vtbl_t::get_relation_type. */
static relation_type_t get_relation_type(const node_t *node, size_t index) {
    const variable_t *expr = (const variable_t *)node;
    if (index == 0 && expr->declarator) {
        return RELATION_DECLARATION;
    }
    return RELATION_NONE;
}

/** @brief Implements @ref node_vtbl_t::calculate. */
static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    const variable_t *expr = (const variable_t *)node;
    if (expr->declarator == get_builtin_declarator()) {
        const builtin_function_t *builtin = find_builtin_function(expr->name);
        return builtin && !state->builtin_bindings_unknown
                   ? make_builtin_function_element(arena, builtin)
                   : make_top_element();
    }
    const lattice_element_t *value = get_from_abstract_state(state, expr->declarator);
    if (!value) {
        value = make_null_element();
        set_in_abstract_state_at(state, expr->declarator, value, node);
    }
    return value;
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    const variable_t *expr = (const variable_t *)node;
    return VIEW_TO_VALUE(expr->name);
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    const variable_t *expr = (const variable_t *)node;
    append_formatted_source(builder, VIEW_TO_VALUE(expr->name));
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    const variable_t *expr = (const variable_t *)node;
    uint32_t index = add_string_to_data_segment_ex(data, expr->name);
    return add_instruction(code, (instruction_t){.opcode = VLOAD, .arg1 = index});
}

/**
 * @brief Generates bytecode for storing a value into a variable.
 * `node`: A pointer to the variable node (must be of variable type)
 */
static instr_index_t
generate_bytecode_assign(const node_t *node, code_builder_t *code, data_builder_t *data) {
    const variable_t *expr = (const variable_t *)node;
    uint32_t index = add_string_to_data_segment_ex(data, expr->name);
    return add_instruction(code, (instruction_t){.opcode = STORE, .arg1 = index});
}

/** @brief Implements node_vtbl_t::analyze_reachability. */
static const lattice_element_t *
analyze_reachability(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    const declarator_t *decl = ((variable_t *)node)->declarator;
    if (decl == get_builtin_declarator())
        return calculate_node(node, *state, (*state)->arena);
    const lattice_element_t *value = get_from_abstract_state(*state, decl);
    return value ? value : make_top_element();
}

/** @brief Numeric representation must be proven at this particular read. */
static bool can_generate_c_code(const node_t *node,
                                const lattice_element_t *value,
                                const c_expression_context_t *context) {
    if (context && context->graph && value) {
        c_value_type_t type = classify_c_value_type(value->type);
        if (value->type == LATTICE_BOOLEAN || value->type == LATTICE_TRUE
            || value->type == LATTICE_FALSE)
            type = C_VALUE_BOOL;
        return c_local_binding(context, ((const variable_t *)node)->declarator, type);
    }
    return is_integer_lattice_element(value) || is_real_lattice_element(value);
}

/** @brief Implements node_vtbl_t::collect_direct_effects. */
static void
collect_direct_effects(const node_t *node, function_summary_t *summary, arena_t *arena) {
    record_function_access(summary, node, FUNCTION_CAPTURE_READ, arena);
}

/** @brief Virtual table for variable expressions. */
static node_vtbl_t variable_vtbl = {
    .type = NODE_VARIABLE,
    .analyze_reachability = analyze_reachability,
    .is_pure = children_are_pure,
    .collect_direct_effects = collect_direct_effects,
    .type_name = L"variable",
    .is_assignable_expression = true,
    .get_data = get_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = no_children,
    .get_child = no_child,
    .get_child_tag = no_tags,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = get_related_count,
    .get_related = get_related,
    .get_relation_type = get_relation_type,
    .calculate = calculate,
    .execute = execute_nothing,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
    .generate_bytecode_assign = generate_bytecode_assign,
    .can_generate_c_code = can_generate_c_code,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = no_indented_c_code,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

expression_t *create_variable_node(arena_t *arena, string_view_t name) {
    variable_t *expr = (variable_t *)alloc_zeroed_from_arena(arena, sizeof(variable_t));
    expr->base.base.base.vtbl = &variable_vtbl;
    expr->name = copy_string_to_arena(arena, name.data, name.length);
    return &expr->base.base;
}

declarator_spec_t *create_declarator_from_variable(const node_t *expr) {
    assert(expr->vtbl->type == NODE_VARIABLE);
    const variable_t *var = (variable_t *)expr;
    declarator_spec_t *decl = (declarator_spec_t *)ALLOC(sizeof(declarator_spec_t));
    decl->name = var->name;
    decl->initial = NULL;
    return decl;
}
