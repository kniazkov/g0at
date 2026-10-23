/**
 * @file character.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the character literal expression.
 */

#include "analysis/lattice.h"
#include "codegen/c_lowering.h"
#include "codegen/code_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "expression.h"
#include "lib/arena.h"
#include "lib/string_ext.h"

/** @brief A character literal expression node. */
typedef struct {
    /** @brief Base expression structure from which character_t inherits. */
    expression_t base;

    /** @brief Lattice element containing the character code. */
    char_constant_element_t element;
} character_t;

/** @brief Appends the single-quoted notation of a character with escape sequences. */
static string_value_t append_character_notation(string_builder_t *builder, uint32_t value) {
    append_char(builder, U'\'');
    switch (value) {
        case 0:
            append_substring(builder, U"\\0", 2);
            break;
        case U'\r':
            append_substring(builder, U"\\r", 2);
            break;
        case U'\n':
            append_substring(builder, U"\\n", 2);
            break;
        case U'\t':
            append_substring(builder, U"\\t", 2);
            break;
        case U'\b':
            append_substring(builder, U"\\b", 2);
            break;
        case U'\\':
            append_substring(builder, U"\\\\", 2);
            break;
        case U'\'':
            append_substring(builder, U"\\'", 2);
            break;
        case U'"':
            append_substring(builder, U"\\\"", 2);
            break;
        default:
            if (value >= 0x10000 || (value >= 0xD800 && value <= 0xDFFF)) {
                append_substring(builder, U"\\U", 2);
                for (int shift = 28; shift >= 0; shift -= 4)
                    append_char(builder, U"0123456789abcdef"[(value >> shift) & 15]);
            } else {
                append_char(builder, (goat_char_t)value);
            }
            break;
    }
    return append_char(builder, U'\'');
}

/** @brief Implements @ref node_vtbl_t::calculate. */
static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    const character_t *expr = (const character_t *)node;
    return &expr->element.base;
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    const character_t *expr = (const character_t *)node;
    string_builder_t builder;
    init_string_builder(&builder, 8);
    return append_character_notation(&builder, expr->element.value);
}

/** @brief Implements @ref node_vtbl_t::get_data. */
static node_display_value_t get_data(const node_t *node) {
    return (node_display_value_t){.text = generate_goat_code(node),
                                  .kind = NODE_DISPLAY_VALUE_STRING_LITERAL};
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    const character_t *expr = (const character_t *)node;
    string_builder_t text;
    init_string_builder(&text, 8);
    append_formatted_source(builder, append_character_notation(&text, expr->element.value));
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    const character_t *expr = (const character_t *)node;
    return add_instruction(code, (instruction_t){.opcode = CLOAD, .arg1 = expr->element.value});
}

/** @brief Implements node_vtbl_t::generate_c_code. */
static c_generated_expression_t generate_c_code(const node_t *node,
                                                c_generation_context_t *context) {
    return c_char_literal(node, context, ((const character_t *)node)->element.value);
}

/** @brief Virtual table for character expressions. */
static node_vtbl_t character_vtbl = {
    .type = NODE_CHARACTER,
    .analyze_reachability = reachability_literal,
    .is_pure = children_are_pure,
    .simplify = no_simplification,
    .collect_direct_effects = collect_child_effects,
    .type_name = U"character",
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
    .generate_c_code = generate_c_code,
    .generate_indented_c_code = no_indented_c_code,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

/** @brief Built-in data type */
static data_type_t data_type = BUILT_IN_DATA_TYPE(U"char");

node_t *create_character_node(arena_t *arena, uint32_t value) {
    character_t *expr = (character_t *)alloc_zeroed_from_arena(arena, sizeof(character_t));
    expr->base.base.vtbl = &character_vtbl;
    expr->base.data_type = &data_type;
    expr->element.base.type = LATTICE_CHAR_CONSTANT;
    expr->element.value = value;
    return &expr->base.base;
}
