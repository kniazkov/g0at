/**
 * @file parenthesized_expression.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of parenthesized expression node.
 */

#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "expression.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"

#include <assert.h>

/** @brief A parenthesized expression node. */
typedef struct {
    /** @brief Base expression structure. */
    expression_t base;

    /**
     * @brief The wrapped inner expression.
     *
     * May be NULL immediately after creation.
     */
    expression_t *inner;
} parenthesized_expression_t;

/** @brief Implements @ref node_vtbl_t::get_child_count. */
static size_t get_child_count(const node_t *node) {
    return 1;
}

/** @brief Implements @ref node_vtbl_t::get_child. */
static node_t *get_child(const node_t *node, size_t index) {
    const parenthesized_expression_t *expr = (const parenthesized_expression_t *)node;
    if (index == 0) {
        return &expr->inner->base;
    }
    return NULL;
}

/** @brief Implements @ref node_vtbl_t::get_child_tag. */
static const wchar_t *get_child_tag(const node_t *node, size_t index) {
    return (index == 0) ? L"expression" : NULL;
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    const parenthesized_expression_t *expr = (const parenthesized_expression_t *)node;
    string_value_t inner_str = generate_goat_code_from_expression(expr->inner);
    string_builder_t sb;
    init_string_builder(&sb, inner_str.length + 2); // +2 for parentheses
    append_char(&sb, L'(');
    append_string_value(&sb, inner_str);
    string_value_t result = append_char(&sb, L')');
    FREE_STRING(inner_str);
    return result;
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    const parenthesized_expression_t *expr = (const parenthesized_expression_t *)node;
    append_static_source(builder, L"(");
    generate_indented_goat_code_from_expression(expr->inner, builder, 0);
    append_static_source(builder, L")");
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    parenthesized_expression_t *expr = (parenthesized_expression_t *)node;
    return generate_bytecode_from_expression(expr->inner, code, data);
}

/** @brief Implements @ref node_vtbl_t::calculate by evaluating the inner expression once. */
static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    const parenthesized_expression_t *expr = (const parenthesized_expression_t *)node;
    return calculate_expression(expr->inner, state, arena);
}

/** @brief Virtual table for parenthesized expression operations. */
static node_vtbl_t expression_parenthesized_vtbl = {
    .type = NODE_EXPRESSION_PARENTHESIZED,
    .type_name = L"parenthesized expression",
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

node_t *create_parenthesized_expression_node(arena_t *arena) {
    parenthesized_expression_t *expr =
        (parenthesized_expression_t *)alloc_zeroed_from_arena(arena,
                                                              sizeof(parenthesized_expression_t));
    expr->base.base.vtbl = &expression_parenthesized_vtbl;
    return &expr->base.base;
}

void fill_parenthesized_expression(node_t *node, expression_t *inner) {
    assert(node->vtbl->type == NODE_EXPRESSION_PARENTHESIZED);
    parenthesized_expression_t *expr = (parenthesized_expression_t *)node;
    expr->inner = inner;
}
