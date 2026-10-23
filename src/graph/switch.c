/** @file switch.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Ordered, non-fallthrough switch with one selector evaluation.
 */
#include "switch.h"

#include "analysis/c_body.h"
#include "analysis/comparison.h"
#include "codegen/c_switch.h"
#include "codegen/code_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "expression.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"

typedef struct {
    statement_t base;
    node_t *selector;
    switch_branch_t *branches;
    size_t count;
} switch_t;

size_t switch_branch_count(const node_t *node) {
    return ((const switch_t *)node)->count;
}

const switch_branch_t *switch_branch(const node_t *node, size_t index) {
    const switch_t *stmt = (const switch_t *)node;
    return index < stmt->count ? &stmt->branches[index] : NULL;
}

static size_t get_child_count(const node_t *node) {
    const switch_t *stmt = (const switch_t *)node;
    size_t count = 1;
    for (size_t i = 0; i < stmt->count; i++)
        count += stmt->branches[i].count + 1;
    return count;
}

static node_t **child_slot(switch_t *stmt, size_t index) {
    if (!index)
        return &stmt->selector;
    index--;
    for (size_t i = 0; i < stmt->count; i++) {
        switch_branch_t *branch = &stmt->branches[i];
        if (index < branch->count)
            return &branch->labels[index];
        if (index == branch->count)
            return &branch->body;
        index -= branch->count + 1;
    }
    return NULL;
}

static node_t *get_child(const node_t *node, size_t index) {
    node_t **slot = child_slot((switch_t *)node, index);
    return slot ? *slot : NULL;
}

static const gchar_t *get_child_tag(const node_t *node, size_t index) {
    if (!index)
        return U"value";
    for (size_t i = 0; i < switch_branch_count(node); i++) {
        const switch_branch_t *branch = switch_branch(node, i);
        if (get_child(node, index) == branch->body)
            return branch->count ? U"case body" : U"default body";
    }
    return U"case value";
}

static bool replace_child(node_t *node, node_t *old, node_t *replacement) {
    for (size_t i = 0; i < get_child_count(node); i++) {
        node_t **slot = child_slot((switch_t *)node, i);
        if (*slot == old) {
            *slot = replacement;
            return true;
        }
    }
    return false;
}

static void merge(abstract_state_t **into, const abstract_state_t *state) {
    abstract_state_t *next =
        *into ? join_abstract_states(*into, state) : clone_abstract_state(state);
    destroy_abstract_state(*into);
    *into = next;
}

static abstract_state_t *execute(node_t *node, abstract_state_t *state, arena_t *arena) {
    if (state->control_flow != FLOW_NORMAL)
        return state;
    const lattice_element_t *selector =
        calculate_expression((expression_t *)get_child(node, 0), state, arena);
    if (state->control_flow != FLOW_NORMAL)
        return state;
    abstract_state_t *pending = clone_abstract_state(state), *exits = NULL;
    for (size_t i = 0; i < switch_branch_count(node) && pending->control_flow == FLOW_NORMAL; i++) {
        const switch_branch_t *branch = switch_branch(node, i);
        if (!branch->count) {
            execute_node(branch->body, pending, arena);
            merge(&exits, pending);
            pending->control_flow = FLOW_UNREACHABLE;
            break;
        }
        for (size_t j = 0; j < branch->count && pending->control_flow == FLOW_NORMAL; j++) {
            const lattice_element_t *label =
                calculate_expression((expression_t *)branch->labels[j], pending, arena);
            if (pending->control_flow != FLOW_NORMAL)
                break;
            abstract_truth_t truth = lattice_truth(lattice_compare(selector, label, COMPARE_EQUAL));
            if (truth == ABSTRACT_TRUE || truth == ABSTRACT_EITHER) {
                abstract_state_t *chosen = clone_abstract_state(pending);
                execute_node(branch->body, chosen, arena);
                merge(&exits, chosen);
                destroy_abstract_state(chosen);
            }
            if (truth == ABSTRACT_TRUE || truth == ABSTRACT_NEVER)
                pending->control_flow = FLOW_UNREACHABLE;
        }
    }
    merge(&exits, pending);
    abstract_state_t old = *state;
    *state = *exits;
    *exits = old;
    destroy_abstract_state(exits);
    destroy_abstract_state(pending);
    return state;
}

static const lattice_element_t *
reach(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    /* Do not publish first-path observations as pointwise facts for other labels/branches. */
    execute(node, *state, (*state)->arena);
    return make_top_element();
}

static string_value_t generate_goat_code(const node_t *node) {
    string_builder_t out = {0};
    string_value_t selector = generate_goat_code_from_node(get_child(node, 0));
    append_static_string(&out, U"switch (");
    append_string_value(&out, selector);
    FREE_STRING(selector);
    append_static_string(&out, U") { ");
    for (size_t i = 0; i < switch_branch_count(node); i++) {
        const switch_branch_t *branch = switch_branch(node, i);
        if (branch->count)
            append_static_string(&out, U"case ");
        else
            append_static_string(&out, U"default");
        for (size_t j = 0; j < branch->count; j++) {
            if (j)
                append_static_string(&out, U", ");
            string_value_t label = generate_goat_code_from_node(branch->labels[j]);
            append_string_value(&out, label);
            FREE_STRING(label);
        }
        append_static_string(&out, U": ");
        /* Emit the branch statements, not an additional object block. */
        const node_t *block = get_node_child(branch->body, 0);
        for (size_t j = 0; j < get_node_child_count(block); j++) {
            string_value_t item = generate_goat_code_from_node(get_node_child(block, j));
            append_string_value(&out, item);
            FREE_STRING(item);
            append_static_string(&out, U" ");
        }
    }
    return append_static_string(&out, U"}");
}

static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    string_value_t selector = generate_goat_code_from_node(get_child(node, 0));
    add_source(builder, indent, U"switch (%s) {", selector.data);
    FREE_STRING(selector);
    for (size_t i = 0; i < switch_branch_count(node); i++) {
        const switch_branch_t *branch = switch_branch(node, i);
        if (branch->count) {
            add_static_source(builder, indent + 1, U"case ");
            for (size_t j = 0; j < branch->count; j++) {
                if (j)
                    append_static_source(builder, U", ");
                append_formatted_source(builder, generate_goat_code_from_node(branch->labels[j]));
            }
            append_static_source(builder, U":");
        } else
            add_static_source(builder, indent + 1, U"default:");
        const node_t *block = get_node_child(branch->body, 0);
        for (size_t j = 0; j < get_node_child_count(block); j++)
            generate_indented_goat_code_from_node(get_node_child(block, j), builder, indent + 2);
    }
    add_static_source(builder, indent, U"}");
}

static void patch(code_builder_t *code, instr_index_t head, instr_index_t target) {
    while (head != UINT32_MAX) {
        instruction_t *in = get_instruction(code, head);
        instr_index_t next = in->arg1;
        in->arg1 = target;
        head = next;
    }
}

static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    instr_index_t first = generate_bytecode_from_node(get_child(node, 0), code, data);
    instr_index_t ends = UINT32_MAX;
    for (size_t i = 0; i < switch_branch_count(node); i++) {
        const switch_branch_t *branch = switch_branch(node, i);
        instr_index_t matches = UINT32_MAX, skip = UINT32_MAX;
        for (size_t j = 0; j < branch->count; j++) {
            add_instruction(code, (instruction_t){.opcode = DUP});
            generate_bytecode_from_node(branch->labels[j], code, data);
            add_instruction(code, (instruction_t){.opcode = EQUAL});
            instr_index_t miss = add_instruction(code, (instruction_t){.opcode = JIF});
            matches = add_instruction(code, (instruction_t){.opcode = JUMP, .arg1 = matches});
            get_instruction(code, miss)->arg1 = get_next_instruction_index(code);
        }
        if (branch->count)
            skip = add_instruction(code, (instruction_t){.opcode = JUMP});
        patch(code, matches, get_next_instruction_index(code));
        add_instruction(code, (instruction_t){.opcode = POP});
        generate_bytecode_from_node(branch->body, code, data);
        ends = add_instruction(code, (instruction_t){.opcode = JUMP, .arg1 = ends});
        if (skip != UINT32_MAX)
            get_instruction(code, skip)->arg1 = get_next_instruction_index(code);
    }
    add_instruction(code, (instruction_t){.opcode = POP});
    patch(code, ends, get_next_instruction_index(code));
    return first;
}

static node_vtbl_t vtbl = {.type = NODE_SWITCH,
                           .type_name = U"switch",
                           .get_data = no_data,
                           .get_property_count = no_properties,
                           .get_property = no_property,
                           .get_child_count = get_child_count,
                           .get_child = get_child,
                           .get_child_tag = get_child_tag,
                           .insert_child_before = no_child_insertion,
                           .replace_child = replace_child,
                           .get_related_count = no_related_nodes,
                           .get_related = no_related_node,
                           .get_relation_type = no_relation_type,
                           .calculate = no_abstract_value,
                           .execute = execute,
                           .analyze_reachability = reach,
                           .is_pure = children_are_pure,
                           .simplify = no_simplification,
                           .collect_direct_effects = collect_child_effects,
                           .generate_goat_code = generate_goat_code,
                           .generate_indented_goat_code = generate_indented_goat_code,
                           .generate_bytecode = generate_bytecode,
                           .can_generate_c_code = c_switch_supported,
                           .generate_c_code = no_c_code,
                           .generate_indented_c_code = c_emit_switch,
                           .generate_bytecode_assign = no_bytecode_assignment,
                           .generate_bytecode_deferred = no_deferred_bytecode};

node_t *
create_switch_node(arena_t *arena, node_t *selector, switch_branch_t *branches, size_t count) {
    switch_t *node = alloc_zeroed_from_arena(arena, sizeof(*node));
    node->base.base.vtbl = &vtbl;
    node->selector = selector;
    node->branches = branches;
    node->count = count;
    return &node->base.base;
}
