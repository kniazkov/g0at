/** @file array.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Array literals with ordered element evaluation.
 */
#include "array.h"

#include "analysis/abstract_state.h"
#include "analysis/reachability.h"
#include "codegen/code_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"

/** @brief Counts array elements. */
static size_t get_child_count(const node_t *node) {
    return ((const array_literal_t *)node)->count;
}

/** @brief Returns an element expression. */
static node_t *get_child(const node_t *node, size_t index) {
    const array_literal_t *expr = (const array_literal_t *)node;
    return index < expr->count ? (node_t *)expr->elements[index] : NULL;
}

/** @brief Replaces an element expression. */
static bool replace_child(node_t *node, node_t *old, node_t *replacement) {
    array_literal_t *expr = (array_literal_t *)node;
    if (!is_expression(replacement->vtbl->type))
        return false;
    for (size_t i = 0; i < expr->count; i++) {
        if ((node_t *)expr->elements[i] == old) {
            expr->elements[i] = (expression_t *)replacement;
            return true;
        }
    }
    return false;
}

/** @brief Generates parseable source. */
static string_value_t generate_goat_code(const node_t *node) {
    const array_literal_t *expr = (const array_literal_t *)node;
    string_builder_t builder = {0};
    append_char(&builder, L'[');
    for (size_t i = 0; i < expr->count; i++) {
        if (i)
            append_static_string(&builder, L", ");
        string_value_t index = generate_goat_code_from_expression(expr->elements[i]);
        append_string_value(&builder, index);
        FREE_STRING(index);
    }
    return append_char(&builder, L']');
}

/** @brief Emits formatted source. */
static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    append_formatted_source(builder, generate_goat_code(node));
}

/** @brief Evaluates elements from left to right and constructs a fresh array. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    const array_literal_t *expr = (const array_literal_t *)node;
    instr_index_t first = 0;
    for (size_t i = 0; i < expr->count; i++) {
        instr_index_t current = generate_bytecode_from_expression(expr->elements[i], code, data);
        if (!i)
            first = current;
    }
    instr_index_t instruction =
        add_instruction(code, (instruction_t){.opcode = ARRAY, .arg1 = expr->count});
    return expr->count ? first : instruction;
}

/** @brief Evaluates element effects conservatively; arrays have no native representation. */
static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    for (size_t i = 0; i < get_child_count(node); i++)
        calculate_expression((expression_t *)get_child(node, i), state, arena);
    forget_abstract_values(state);
    return make_top_element();
}

/** @brief Visits elements in source order and invalidates mutable facts. */
static const lattice_element_t *
analyze_reachability(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    visit_reachable_children(node, state, collector);
    forget_abstract_values(*state);
    return make_top_element();
}

/** @brief Virtual table for array literals. */
static node_vtbl_t vtbl = {.type = NODE_ARRAY,
                           .type_name = L"array",
                           .is_assignable_expression = false,
                           .analyze_reachability = analyze_reachability,
                           .is_pure = not_pure,
                           .simplify = no_simplification,
                           .collect_direct_effects = unknown_direct_effects,
                           .get_data = no_data,
                           .get_property_count = no_properties,
                           .get_property = no_property,
                           .get_child_count = get_child_count,
                           .get_child = get_child,
                           .get_child_tag = no_tags,
                           .insert_child_before = no_child_insertion,
                           .replace_child = replace_child,
                           .get_related_count = no_related_nodes,
                           .get_related = no_related_node,
                           .get_relation_type = no_relation_type,
                           .calculate = calculate,
                           .execute = execute_nothing,
                           .generate_goat_code = generate_goat_code,
                           .generate_indented_goat_code = generate_indented_goat_code,
                           .generate_bytecode = generate_bytecode,
                           .generate_bytecode_assign = no_bytecode_assignment,
                           .can_generate_c_code = cannot_generate_c_code,
                           .generate_c_code = no_c_code,
                           .generate_indented_c_code = no_indented_c_code,
                           .generate_bytecode_deferred = no_deferred_bytecode};

node_t *create_array_node(arena_t *arena) {
    array_literal_t *expr = alloc_zeroed_from_arena(arena, sizeof(*expr));
    expr->base.base.vtbl = &vtbl;
    return &expr->base.base;
}

void set_array_elements(node_t *node, arena_t *arena, expression_t **elements, size_t count) {
    array_literal_t *expr = (array_literal_t *)node;
    expr->count = count;
    if (count) {
        expr->elements = alloc_from_arena(arena, count * sizeof(*elements));
        memcpy(expr->elements, elements, count * sizeof(*elements));
    }
}
