/**
 * @file integer.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the integer literal expression.
 */

#include "analysis/lattice.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "expression.h"
#include "lib/arena.h"
#include "lib/split64.h"
#include "lib/string_ext.h"

/** @brief An integer literal expression node. */
typedef struct {
    /** @brief Base expression structure from which integer_t inherits. */
    expression_t base;

    /** @brief Lattice element containing 64-bit signed integer value. */
    integer_constant_element_t element;
} integer_t;

/** @brief Implements @ref node_vtbl_t::calculate. */
static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    const integer_t *expr = (const integer_t *)node;
    return &expr->element.base;
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    const integer_t *expr = (const integer_t *)node;
    return format_string(L"%ld", expr->element.value);
}

/** @brief Implements @ref node_vtbl_t::get_data. */
static node_display_value_t get_data(const node_t *node) {
    return (node_display_value_t){.text = generate_goat_code(node),
                                  .kind = NODE_DISPLAY_VALUE_PLAIN};
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    const integer_t *expr = (const integer_t *)node;
    append_formatted_source(builder, format_string(L"%ld", expr->element.value));
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    const integer_t *expr = (const integer_t *)node;
    int64_t value = expr->element.value;
    instr_index_t first;
    if (value > INT32_MAX || value < INT32_MIN) {
        split64_t s;
        s.int_value = value;
        first = add_instruction(code, (instruction_t){.opcode = ARG, .arg1 = s.parts[0]});
        add_instruction(code, (instruction_t){.opcode = ILOAD64, .arg1 = s.parts[1]});
    } else {
        first = add_instruction(code, (instruction_t){.opcode = ILOAD32, .arg1 = value});
    }
    return first;
}

/** @brief Virtual table for integer expressions. */
static node_vtbl_t integer_vtbl = {
    .type = NODE_INTEGER,
    .analyze_reachability = reachability_literal,
    .is_pure = children_are_pure,
    .simplify = no_simplification,
    .collect_direct_effects = collect_child_effects,
    .type_name = L"integer",
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
    .can_generate_c_code = numeric_literal_c_code,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = no_indented_c_code,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

/** @brief Built-in data type */
static data_type_t data_type = BUILT_IN_DATA_TYPE(L"int");

node_t *create_integer_node(arena_t *arena, int64_t value) {
    integer_t *expr = (integer_t *)alloc_zeroed_from_arena(arena, sizeof(integer_t));
    expr->base.base.vtbl = &integer_vtbl;
    expr->base.data_type = &data_type;
    expr->element.base.type = LATTICE_INTEGER_CONSTANT;
    expr->element.value = value;
    return &expr->base.base;
}
