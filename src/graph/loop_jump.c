/** @file loop_jump.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Non-local loop transfers with explicit abstract exit accumulators.
 */
#include "analysis/c_body.h"
#include "codegen/c_control.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "lib/arena.h"
#include "loop.h"

#include <assert.h>

static size_t get_child_count(const node_t *node) {
    return 0;
}

static node_t *get_child(const node_t *node, size_t index) {
    return NULL;
}

static abstract_state_t *execute(node_t *node, abstract_state_t *state, arena_t *arena) {
    if (state->control_flow != FLOW_NORMAL)
        return state;
    assert(state->loop);
    abstract_state_t **target =
        node->vtbl->type == NODE_BREAK ? &state->loop->breaks : &state->loop->continues;
    abstract_state_t *joined =
        *target ? join_abstract_states(*target, state) : clone_abstract_state(state);
    destroy_abstract_state(*target);
    *target = joined;
    state->control_flow = FLOW_UNREACHABLE;
    return state;
}

static const lattice_element_t *
analyze_reachability(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    execute(node, *state, (*state)->arena);
    return make_top_element();
}

static string_value_t generate_goat_code(const node_t *node) {
    return node->vtbl->type == NODE_BREAK ? STATIC_STRING(U"break;") : STATIC_STRING(U"continue;");
}

static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    add_formatted_source(builder, indent, generate_goat_code(node));
}

static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    return generate_loop_jump(node, code);
}

static node_vtbl_t jump_vtbl = {
    .type = NODE_BREAK,
    .type_name = U"break",
    .get_data = no_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = get_child_count,
    .get_child = get_child,
    .get_child_tag = no_tags,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = no_abstract_value,
    .execute = execute,
    .analyze_reachability = analyze_reachability,
    .is_pure = children_are_pure,
    .simplify = no_simplification,
    .collect_direct_effects = collect_child_effects,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
    .can_generate_c_code = c_body_children,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = c_emit_loop_jump,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

node_t *create_loop_jump_node(arena_t *arena, bool next) {
    static node_vtbl_t continue_vtbl;
    continue_vtbl = jump_vtbl;
    continue_vtbl.type = NODE_CONTINUE;
    continue_vtbl.type_name = U"continue";
    statement_t *node = alloc_zeroed_from_arena(arena, sizeof(*node));
    node->base.vtbl = next ? &continue_vtbl : &jump_vtbl;
    return &node->base;
}
