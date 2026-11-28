/** @file byte_array.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Compact byte literal frontend; fresh runtime allocation.
 */
#include "analysis/lattice.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "expression.h"
#include "lib/memory/arena.h"
#include "lib/strings/string_ext.h"

#include <stdlib.h>
#include <string.h>

/** @brief Arena-owned raw bytes, not runtime objects. */
typedef struct {
    expression_t base;
    size_t length;
    uint8_t *bytes;
} byte_literal_t;

static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    return make_user_defined_object_element();
}

static string_value_t generate_goat_code(const node_t *node) {
    const byte_literal_t *literal = (const byte_literal_t *)node;
    string_builder_t builder;
    init_string_builder(&builder, 0);
    append_char(&builder, U'#');
    for (size_t i = 0; i < literal->length; ++i) {
        append_char(&builder, U"0123456789ABCDEF"[literal->bytes[i] >> 4]);
        append_char(&builder, U"0123456789ABCDEF"[literal->bytes[i] & 15]);
    }
    return (string_value_t){builder.data, builder.length, true};
}

static node_display_value_t get_data(const node_t *node) {
    return (node_display_value_t){.text = generate_goat_code(node),
                                  .kind = NODE_DISPLAY_VALUE_STRING_LITERAL};
}

static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    append_formatted_source(builder, generate_goat_code(node));
}

/** @brief Emit an immutable raw constant and a fresh-allocation instruction. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    const byte_literal_t *literal = (const byte_literal_t *)node;
    uint32_t id = add_bytes_to_data_segment(data, literal->bytes, literal->length);
    return add_instruction(code, (instruction_t){.opcode = BYTELOAD, .arg1 = id});
}

static node_vtbl_t byte_array_vtbl = {
    .type = NODE_BYTE_ARRAY,
    .analyze_reachability = reachability_literal,
    .is_pure = not_pure,
    .simplify = no_simplification,
    .collect_direct_effects = collect_child_effects,
    .type_name = U"byte_array",
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

static data_type_t data_type = BUILT_IN_DATA_TYPE(U"ByteArray");

node_t *create_byte_array_node(arena_t *arena, const uint8_t *bytes, size_t length) {
    byte_literal_t *node = alloc_zeroed_from_arena(arena, sizeof(*node));
    node->base.base.vtbl = &byte_array_vtbl;
    node->base.data_type = &data_type;
    node->length = length;
    if (length) {
        node->bytes = alloc_from_arena(arena, length);
        memcpy(node->bytes, bytes, length);
    }
    return &node->base.base;
}
