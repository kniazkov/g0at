/**
 * @file static_string.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the static string expression.
 */

#include "analysis/lattice.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "expression.h"
#include "lib/arena.h"
#include "lib/string_ext.h"

#include <memory.h>

/**
 * @brief A static string expression node.
 *
 * A static string is a literal value that appears in the source code and is immutable.
 */
typedef struct {
    /** @brief Base expression structure from which static_string_t inherits. */
    expression_t base;

    /** @brief Lattice element containing the string. */
    string_constant_element_t element;
} static_string_t;

/** @brief Implements @ref node_vtbl_t::get_data. */
static node_display_value_t get_data(const node_t *node) {
    const static_string_t *expr = (const static_string_t *)node;
    return (node_display_value_t){.text = VIEW_TO_VALUE(expr->element.value),
                                  .kind = NODE_DISPLAY_VALUE_STRING_LITERAL};
}

/** @brief Implements @ref node_vtbl_t::calculate. */
static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    const static_string_t *expr = (const static_string_t *)node;
    return &expr->element.base;
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    const static_string_t *expr = (const static_string_t *)node;
    return string_to_string_notation(L"", VIEW_TO_VALUE(expr->element.value));
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    const static_string_t *expr = (const static_string_t *)node;
    append_formatted_source(builder,
                            string_to_string_notation(L"", VIEW_TO_VALUE(expr->element.value)));
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    const static_string_t *expr = (const static_string_t *)node;
    uint32_t index = add_string_to_data_segment_ex(data, expr->element.value);
    return add_instruction(code, (instruction_t){.opcode = SLOAD, .arg1 = index});
}

/** @brief Virtual table for static string expressions. */
static node_vtbl_t static_string_vtbl = {
    .type = NODE_STATIC_STRING,
    .analyze_reachability = reachability_literal,
    .is_pure = children_are_pure,
    .collect_direct_effects = collect_child_effects,
    .type_name = L"static string",
    .get_data = get_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = no_children,
    .get_child = no_child,
    .get_child_tag = no_tags,
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

node_t *create_static_string_node(arena_t *arena, const wchar_t *data, size_t length) {
    static_string_t *expr =
        (static_string_t *)alloc_zeroed_from_arena(arena, sizeof(static_string_t));
    expr->base.base.vtbl = &static_string_vtbl;
    expr->element.base.type = LATTICE_STRING_CONSTANT;
    expr->element.value = copy_string_to_arena(arena, data, length);
    return &expr->base.base;
}
