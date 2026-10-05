/** @file for.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Scoped C-style loops with conservative abstract execution.
 */
#include "analysis/abstract_state.h"
#include "analysis/c_body.h"
#include "analysis/function_call_graph.h"
#include "analysis/lattice.h"
#include "codegen/c_control.h"
#include "codegen/code_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "expression.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "statement.h"

/** @brief Ordered header slots and body; the loop owns one lexical context. */
typedef struct {
    statement_t base;    /**< Statement base. */
    node_t *children[4]; /**< Initializer, condition, step, body. */
} for_t;

/** @brief Implements node_vtbl_t::get_child_count. */
static size_t get_child_count(const node_t *node) {
    return 4;
}

/** @brief Implements node_vtbl_t::get_child. */
static node_t *get_child(const node_t *node, size_t index) {
    return index < 4 ? ((const for_t *)node)->children[index] : NULL;
}

/** @brief Implements node_vtbl_t::get_child_tag. */
static const wchar_t *get_child_tag(const node_t *node, size_t index) {
    static const wchar_t *tags[] = {L"initial", L"condition", L"step", L"body"};
    return index < 4 ? tags[index] : NULL;
}

/** @brief Implements node_vtbl_t::replace_child. */
static bool replace_child(node_t *node, node_t *old_child, node_t *new_child) {
    for_t *loop = (for_t *)node;
    for (size_t i = 0; i < 4; i++) {
        if (loop->children[i] != old_child)
            continue;
        if (i == 1 ? !is_expression(new_child->vtbl->type)
                   : !(is_statement(new_child->vtbl->type)
                       || is_branch_or_loop(new_child->vtbl->type)))
            return false;
        loop->children[i] = new_child;
        return true;
    }
    return false;
}

/** @brief Computes a widened loop invariant without executing an unbounded number of passes. */
static abstract_state_t *execute(node_t *node, abstract_state_t *state, arena_t *arena) {
    if (state->control_flow != FLOW_NORMAL)
        return state;
    execute_node(get_child(node, 0), state, arena);
    if (state->control_flow != FLOW_NORMAL)
        return state;
    abstract_state_t *head = clone_abstract_state(state);
    bool stable = false;
    for (size_t pass = 0; pass < 16 && *state->call_budget; pass++) {
        --*state->call_budget;
        abstract_state_t *back = clone_abstract_state(head);
        const lattice_element_t *condition =
            calculate_expression((expression_t *)get_child(node, 1), back, arena);
        if (back->control_flow == FLOW_NORMAL && lattice_truth(condition) != ABSTRACT_FALSE) {
            execute_node(get_child(node, 3), back, arena);
            if (back->control_flow == FLOW_NORMAL)
                execute_node(get_child(node, 2), back, arena);
        } else {
            back->control_flow = FLOW_UNREACHABLE;
        }
        abstract_state_t *next = widen_loop_state(head, back, &stable);
        destroy_abstract_state(back);
        destroy_abstract_state(head);
        head = next;
        if (stable)
            break;
    }
    if (stable) {
        const lattice_element_t *condition =
            calculate_expression((expression_t *)get_child(node, 1), head, arena);
        if (head->control_flow == FLOW_NORMAL && lattice_truth(condition) == ABSTRACT_TRUE)
            head->control_flow = FLOW_UNREACHABLE;
    } else {
        forget_abstract_values(head);
        if (head->call_graph_node)
            head->call_graph_node->complete = false;
        if (head->return_value)
            *head->return_value = make_top_element();
        if (head->type_analysis_incomplete)
            *head->type_analysis_incomplete = true;
    }
    /* Keep the state address held by expression and function callers. */
    abstract_state_t previous = *state;
    *state = *head;
    *head = previous;
    destroy_abstract_state(head);
    return state;
}

/** @brief Does not turn first-iteration observations into proofs for later iterations. */
static const lattice_element_t *
analyze_reachability(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    execute(node, *state, (*state)->arena);
    return make_top_element();
}

/** @brief Formats the header, stripping statement terminators from its outer slots. */
static string_value_t header_source(const node_t *node) {
    string_builder_t builder = {0};
    append_static_string(&builder, L"for (");
    for (size_t i = 0; i < 3; i++) {
        string_value_t text = generate_goat_code_from_node(get_child(node, i));
        if (i != 1 && text.length && text.data[text.length - 1] == L';')
            text.length--;
        append_string_value(&builder, text);
        FREE_STRING(text);
        if (i != 2)
            append_static_string(&builder, L"; ");
    }
    return append_static_string(&builder, L") ");
}

/** @brief Implements node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    string_builder_t builder = {0};
    string_value_t header = header_source(node);
    append_string_value(&builder, header);
    FREE_STRING(header);
    string_value_t body = generate_goat_code_from_node(get_child(node, 3));
    append_string_value(&builder, body);
    FREE_STRING(body);
    return append_static_string(&builder, L"");
}

/** @brief Implements node_vtbl_t::generate_indented_goat_code. */
static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    add_formatted_source(builder, indent, header_source(node));
    generate_indented_goat_code_from_statement((statement_t *)get_child(node, 3),
                                               builder,
                                               indent + 1);
}

/** @brief Emits one loop context and a condition/body/step back edge. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    instr_index_t first = add_instruction(code, (instruction_t){.opcode = ENTER});
    generate_bytecode_from_node(get_child(node, 0), code, data);
    instr_index_t test = get_next_instruction_index(code);
    generate_bytecode_from_node(get_child(node, 1), code, data);
    instr_index_t exit = add_instruction(code, (instruction_t){.opcode = JIF});
    generate_bytecode_from_node(get_child(node, 3), code, data);
    generate_bytecode_from_node(get_child(node, 2), code, data);
    add_instruction(code, (instruction_t){.opcode = JUMP, .arg1 = test});
    get_instruction(code, exit)->arg1 = get_next_instruction_index(code);
    add_instruction(code, (instruction_t){.opcode = LEAVE});
    add_instruction(code, (instruction_t){.opcode = POP});
    return first;
}

/** @brief Loop virtual methods. */
static node_vtbl_t for_vtbl = {
    .type = NODE_FOR,
    .type_name = L"for",
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
    .analyze_reachability = analyze_reachability,
    .is_pure = children_are_pure,
    .simplify = no_simplification,
    .collect_direct_effects = collect_child_effects,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
    .can_generate_c_code = c_body_children,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = c_emit_for,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

node_t *create_for_node(arena_t *arena,
                        statement_t *initial,
                        expression_t *condition,
                        statement_t *step,
                        statement_t *body) {
    for_t *loop = alloc_zeroed_from_arena(arena, sizeof(*loop));
    loop->base.base.vtbl = &for_vtbl;
    loop->children[0] = (node_t *)initial;
    loop->children[1] = (node_t *)condition;
    loop->children[2] = (node_t *)step;
    loop->children[3] = (node_t *)body;
    return &loop->base.base;
}
