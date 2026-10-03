/** @file replacement.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Replacement nodes retain history; bytecode uses the result, C uses the original.
 */
#include "replacement.h"

#include "analysis/function_effects.h"
#include "analysis/reachability.h"
#include "common_methods.h"
#include "lib/arena.h"

#include <assert.h>

/** @brief The original may share descendants with the executable result. */
typedef struct {
    node_t *original;
    node_t *result;
} replacement_children_t;

typedef struct {
    expression_t base;
    replacement_children_t children;
} expression_replacement_t;

typedef struct {
    statement_t base;
    replacement_children_t children;
} statement_replacement_t;

static const replacement_children_t *children(const node_t *node) {
    return node->vtbl->type == NODE_EXPRESSION_REPLACEMENT
               ? &((const expression_replacement_t *)node)->children
               : &((const statement_replacement_t *)node)->children;
}

const node_t *replacement_result(const node_t *node) {
    while (is_replacement(node))
        node = children(node)->result;
    return node;
}

const node_t *replacement_original(const node_t *node) {
    while (is_replacement(node))
        node = children(node)->original;
    return node;
}

/** @brief Implements node_vtbl_t::get_child_count. */
static size_t get_child_count(const node_t *node) {
    return 2;
}

/** @brief Implements node_vtbl_t::get_child. */
static node_t *get_child(const node_t *node, size_t index) {
    return index == 0 ? children(node)->original : index == 1 ? children(node)->result : NULL;
}

/** @brief Implements node_vtbl_t::get_child_tag. */
static const wchar_t *get_child_tag(const node_t *node, size_t index) {
    return index == 0 ? L"original" : index == 1 ? L"replacement" : NULL;
}

/** @brief Implements node_vtbl_t::calculate through the executable expression. */
static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    return calculate_expression((expression_t *)children(node)->result, state, arena);
}

/** @brief Implements node_vtbl_t::execute through the executable statement. */
static abstract_state_t *execute(node_t *node, abstract_state_t *state, arena_t *arena) {
    return execute_node(children(node)->result, state, arena);
}

/** @brief Implements node_vtbl_t::analyze_reachability without visiting history. */
static const lattice_element_t *
analyze_reachability(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    return visit_reachable_node(children(node)->result, state, collector);
}

/** @brief Implements node_vtbl_t::is_pure from the executable child's cache. */
static bool is_pure(const node_t *node) {
    return node_has_flag(children(node)->result, NODE_FLAG_PURE);
}

/** @brief Implements node_vtbl_t::collect_direct_effects without archived effects. */
static void collect_effects(const node_t *node, function_summary_t *summary, arena_t *arena) {
    collect_node_direct_effects(children(node)->result, summary, arena);
}

/** @brief Implements node_vtbl_t::can_generate_c_code. */
static bool can_generate_c_code(const node_t *node,
                                const lattice_element_t *value,
                                const c_expression_context_t *context) {
    const node_t *result = children(node)->result;
    return result->vtbl->can_generate_c_code(result, value, context);
}

/** @brief Implements node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    return generate_goat_code_from_node(children(node)->result);
}

/** @brief Implements node_vtbl_t::generate_indented_goat_code. */
static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    generate_indented_goat_code_from_node(children(node)->result, builder, indent);
}

/** @brief Implements node_vtbl_t::generate_c_code through the unsimplified expression. */
static c_generated_expression_t generate_c_code(const node_t *node,
                                                c_generation_context_t *context) {
    return generate_c_code_from_node(replacement_original(node), context);
}

/** @brief Implements node_vtbl_t::generate_indented_c_code through the original statement. */
static bool generate_indented_c_code(const node_t *node,
                                     c_generation_context_t *context,
                                     source_builder_t *builder,
                                     size_t indent) {
    return generate_indented_c_code_from_node(replacement_original(node), context, builder, indent);
}

/** @brief Implements node_vtbl_t::generate_bytecode without emitting the original. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    return generate_bytecode_from_node(children(node)->result, code, data);
}

/** @brief Implements node_vtbl_t::generate_bytecode_deferred. */
static bool
generate_bytecode_deferred(const node_t *node, code_builder_t *code, data_builder_t *data) {
    return generate_deferred_bytecode_from_node(children(node)->result, code, data);
}

/** @brief Virtual methods for expression replacements. */
static node_vtbl_t expression_vtbl = {
    .type = NODE_EXPRESSION_REPLACEMENT,
    .type_name = L"expression replacement",
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
    .analyze_reachability = analyze_reachability,
    .is_pure = is_pure,
    .simplify = no_simplification,
    .collect_direct_effects = collect_effects,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .can_generate_c_code = can_generate_c_code,
    .generate_c_code = generate_c_code,
    .generate_indented_c_code = generate_indented_c_code,
    .generate_bytecode = generate_bytecode,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = generate_bytecode_deferred,
};

expression_t *
create_expression_replacement(arena_t *arena, expression_t *original, expression_t *replacement) {
    assert(original && replacement && original != replacement);
    expression_replacement_t *node = alloc_zeroed_from_arena(arena, sizeof(*node));
    node->base.base = original->base;
    node->base.base.vtbl = &expression_vtbl;
    node->base.base.flags = replacement->base.flags;
    node->base.data_type = replacement->data_type;
    node->base.immediate_value = replacement->immediate_value;
    node->children = (replacement_children_t){&original->base, &replacement->base};
    return &node->base;
}

/** @brief Virtual methods for statement replacements. */
static node_vtbl_t statement_vtbl = {
    .type = NODE_STATEMENT_REPLACEMENT,
    .type_name = L"statement replacement",
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
    .calculate = no_abstract_value,
    .execute = execute,
    .analyze_reachability = analyze_reachability,
    .is_pure = is_pure,
    .simplify = no_simplification,
    .collect_direct_effects = collect_effects,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .can_generate_c_code = can_generate_c_code,
    .generate_c_code = generate_c_code,
    .generate_indented_c_code = generate_indented_c_code,
    .generate_bytecode = generate_bytecode,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = generate_bytecode_deferred,
};

statement_t *
create_statement_replacement(arena_t *arena, statement_t *original, statement_t *replacement) {
    assert(original && replacement && original != replacement);
    statement_replacement_t *node = alloc_zeroed_from_arena(arena, sizeof(*node));
    node->base.base = original->base;
    node->base.base.vtbl = &statement_vtbl;
    node->base.base.flags = replacement->base.flags;
    node->children = (replacement_children_t){&original->base, &replacement->base};
    return &node->base;
}
