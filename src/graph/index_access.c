/** @file index_access.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Indexed reads and writes; receivers and indices are evaluated once in source order.
 */
#include "index_access.h"

#include "analysis/abstract_state.h"
#include "analysis/reachability.h"
#include "codegen/code_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"

/** @brief Includes the receiver followed by every index. */
static size_t get_child_count(const node_t *node) {
    return ((const index_access_t *)node)->count + 1;
}

/** @brief Returns one receiver/index child. */
static node_t *get_child(const node_t *node, size_t index) {
    const index_access_t *expr = (const index_access_t *)node;
    return index == 0             ? (node_t *)expr->object
           : index <= expr->count ? (node_t *)expr->indices[index - 1]
                                  : NULL;
}

/** @brief Replaces one receiver/index expression. */
static bool replace_child(node_t *node, node_t *old, node_t *replacement) {
    index_access_t *expr = (index_access_t *)node;
    if (!is_expression(replacement->vtbl->type))
        return false;
    if ((node_t *)expr->object == old) {
        expr->object = (expression_t *)replacement;
        return true;
    }
    for (size_t i = 0; i < expr->count; i++) {
        if ((node_t *)expr->indices[i] == old) {
            expr->indices[i] = (expression_t *)replacement;
            return true;
        }
    }
    return false;
}

/** @brief Generates parseable source. */
static string_value_t generate_goat_code(const node_t *node) {
    const index_access_t *expr = (const index_access_t *)node;
    string_builder_t builder = {0};
    string_value_t object = generate_goat_code_from_expression(expr->object);
    append_string_value(&builder, object);
    FREE_STRING(object);
    append_char(&builder, L'[');
    for (size_t i = 0; i < expr->count; i++) {
        if (i)
            append_static_string(&builder, L", ");
        string_value_t index = generate_goat_code_from_expression(expr->indices[i]);
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

/** @brief Emits receiver and indices, retaining the receiver only for CALLM. */
static instr_index_t operands(const index_access_t *expr,
                              code_builder_t *code,
                              data_builder_t *data,
                              bool retain_receiver) {
    instr_index_t first = generate_bytecode_from_expression(expr->object, code, data);
    if (retain_receiver)
        add_instruction(code, (instruction_t){.opcode = DUP});
    for (size_t i = 0; i < expr->count; i++)
        generate_bytecode_from_expression(expr->indices[i], code, data);
    return first;
}

instr_index_t generate_index_read(node_t *node,
                                  code_builder_t *code,
                                  data_builder_t *data,
                                  bool retain_receiver) {
    const index_access_t *expr = (const index_access_t *)node;
    instr_index_t first = operands(expr, code, data, retain_receiver);
    add_instruction(code, (instruction_t){.opcode = GETINDEX, .arg1 = expr->count});
    return first;
}

/** @brief Emits a read. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    return generate_index_read(node, code, data, false);
}

/** @brief RHS is already on the stack, as with named property assignment. */
static instr_index_t
generate_bytecode_assign(const node_t *node, code_builder_t *code, data_builder_t *data) {
    const index_access_t *expr = (const index_access_t *)node;
    instr_index_t first = operands(expr, code, data, false);
    add_instruction(code, (instruction_t){.opcode = SETINDEX, .arg1 = expr->count});
    return first;
}

/** @brief Indexing can invoke arbitrary type behavior, so invalidate mutable facts. */
static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    for (size_t i = 0; i < get_child_count(node); i++)
        calculate_expression((expression_t *)get_child(node, i), state, arena);
    forget_abstract_values(state);
    return make_top_element();
}

/** @brief Visits receiver/indices in evaluation order and invalidates mutable facts. */
static const lattice_element_t *
analyze_reachability(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    visit_reachable_children(node, state, collector);
    forget_abstract_values(*state);
    return make_top_element();
}

/** @brief Virtual table for index expressions. */
static node_vtbl_t vtbl = {.type = NODE_INDEX_ACCESS,
                           .type_name = L"index_access",
                           .is_assignable_expression = true,
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
                           .generate_bytecode_assign = generate_bytecode_assign,
                           .can_generate_c_code = cannot_generate_c_code,
                           .generate_c_code = no_c_code,
                           .generate_indented_c_code = no_indented_c_code,
                           .generate_bytecode_deferred = no_deferred_bytecode};

node_t *create_index_access_node(arena_t *arena, expression_t *object) {
    index_access_t *expr = alloc_zeroed_from_arena(arena, sizeof(*expr));
    expr->base.base.base.vtbl = &vtbl;
    expr->object = object;
    return &expr->base.base.base;
}

void set_index_arguments(node_t *node, arena_t *arena, expression_t **indices, size_t count) {
    index_access_t *expr = (index_access_t *)node;
    expr->count = count;
    if (count) {
        expr->indices = alloc_from_arena(arena, count * sizeof(*indices));
        memcpy(expr->indices, indices, count * sizeof(*indices));
    }
}
