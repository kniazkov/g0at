/** @file for.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Scoped pre-test and post-test loops with abstract transfer tracking.
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
#include "loop.h"
#include "statement.h"

#include <stdlib.h>

/** @brief Ordered header slots and body; the loop owns one lexical context. */
typedef struct {
    statement_t base;                /**< Statement base. */
    instr_index_t breaks, continues; /**< Bytecode patch chains. */
    node_t *children[4];             /**< Initializer, condition, step, body. */
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

/** @brief Joins another normal exit or back edge into an owned accumulator. */
static void accumulate(abstract_state_t **target, const abstract_state_t *source) {
    if (!source || source->control_flow != FLOW_NORMAL)
        return;
    abstract_state_t *joined =
        *target ? join_abstract_states(*target, source) : clone_abstract_state(source);
    destroy_abstract_state(*target);
    *target = joined;
}

/** @brief Computes loop invariants while keeping breaks and continues out of ordinary joins. */
static abstract_state_t *execute(node_t *node, abstract_state_t *state, arena_t *arena) {
    if (state->control_flow != FLOW_NORMAL)
        return state;
    execute_node(get_child(node, 0), state, arena);
    if (state->control_flow != FLOW_NORMAL)
        return state;
    bool post = node->vtbl->type == NODE_DO_WHILE;
    abstract_loop_t *outer = state->loop;
    abstract_state_t *head = clone_abstract_state(state), *exits = NULL;
    bool stable = false;
    for (size_t pass = 0; pass < 16 && *state->call_budget; pass++) {
        --*state->call_budget;
        abstract_loop_t transfers = {0};
        abstract_state_t *back = clone_abstract_state(head);
        back->loop = &transfers;
        if (!post) {
            abstract_truth_t truth = lattice_truth(
                calculate_expression((expression_t *)get_child(node, 1), back, arena));
            if (truth != ABSTRACT_TRUE && truth != ABSTRACT_NEVER)
                accumulate(&exits, back);
            if (truth == ABSTRACT_FALSE || truth == ABSTRACT_NEVER)
                back->control_flow = FLOW_UNREACHABLE;
        }
        if (back->control_flow == FLOW_NORMAL)
            execute_node(get_child(node, 3), back, arena);
        accumulate(&exits, transfers.breaks);
        if (transfers.continues) {
            abstract_state_t *joined = join_abstract_states(back, transfers.continues);
            destroy_abstract_state(back);
            back = joined;
        }
        if (back->control_flow == FLOW_NORMAL) {
            execute_node(get_child(node, 2), back, arena);
            if (post) {
                abstract_truth_t truth = lattice_truth(
                    calculate_expression((expression_t *)get_child(node, 1), back, arena));
                if (truth != ABSTRACT_TRUE && truth != ABSTRACT_NEVER)
                    accumulate(&exits, back);
                if (truth == ABSTRACT_FALSE || truth == ABSTRACT_NEVER)
                    back->control_flow = FLOW_UNREACHABLE;
            }
        }
        back->loop = outer;
        abstract_state_t *next = widen_loop_state(head, back, &stable);
        destroy_abstract_state(back);
        destroy_abstract_state(transfers.breaks);
        destroy_abstract_state(transfers.continues);
        destroy_abstract_state(head);
        head = next;
        if (stable)
            break;
    }
    if (!stable) {
        forget_abstract_values(head);
        if (head->call_graph_node)
            head->call_graph_node->complete = false;
        if (head->return_value)
            *head->return_value = make_top_element();
        if (head->type_analysis_incomplete)
            *head->type_analysis_incomplete = true;
        head->control_flow = FLOW_NORMAL;
        accumulate(&exits, head);
    }
    if (!exits) {
        exits = clone_abstract_state(head);
        exits->control_flow = FLOW_UNREACHABLE;
    }
    exits->loop = outer;
    abstract_state_t previous = *state;
    *state = *exits;
    *exits = previous;
    destroy_abstract_state(exits);
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
    if (node->vtbl->type != NODE_FOR) {
        string_value_t condition = generate_goat_code_from_node(get_child(node, 1));
        string_value_t result = format_string(L"while (%s) ", condition.data);
        FREE_STRING(condition);
        return result;
    }
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
    if (node->vtbl->type == NODE_DO_WHILE) {
        string_value_t body = generate_goat_code_from_node(get_child(node, 3));
        string_value_t header = header_source(node);
        string_value_t result = format_string(L"do %s %s;", body.data, header.data);
        FREE_STRING(body);
        FREE_STRING(header);
        return result;
    }
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
    if (node->vtbl->type == NODE_DO_WHILE) {
        add_static_source(builder, indent, L"do ");
        generate_indented_goat_code_from_node(get_child(node, 3), builder, indent + 1);
        add_formatted_source(builder, indent, header_source(node));
        append_static_source(builder, L";");
        return;
    }
    add_formatted_source(builder, indent, header_source(node));
    generate_indented_goat_code_from_statement((statement_t *)get_child(node, 3),
                                               builder,
                                               indent + 1);
}

bool is_loop_node(const node_t *node) {
    return node
           && (node->vtbl->type == NODE_FOR || node->vtbl->type == NODE_WHILE
               || node->vtbl->type == NODE_DO_WHILE);
}

/** @brief Ignore nested loops and function bodies when inspecting transfers. */
static bool has_jump(const node_t *node, bool next) {
    if (is_loop_node(node) || node->vtbl->type == NODE_FUNCTION_OBJECT)
        return false;
    if (node->vtbl->type == (next ? NODE_CONTINUE : NODE_BREAK))
        return true;
    for (size_t i = 0; i < get_node_child_count(node); i++)
        if (has_jump(get_node_child(node, i), next))
            return true;
    return false;
}

bool loop_has_jump(const node_t *node, bool next) {
    return has_jump(get_child(node, 3), next);
}

instr_index_t generate_loop_jump(node_t *node, code_builder_t *code) {
    bool next = node->vtbl->type == NODE_CONTINUE;
    for (node_t *child = node, *parent = node->parent; parent;
         child = parent, parent = parent->parent) {
        if (parent->vtbl->type == NODE_FUNCTION_OBJECT)
            break;
        if (!is_loop_node(parent) || get_child(parent, 3) != child)
            continue;
        for_t *loop = (for_t *)parent;
        instr_index_t *head = next ? &loop->continues : &loop->breaks;
        instr_index_t result =
            add_instruction(code, (instruction_t){.opcode = LOOP_JUMP, .arg1 = *head});
        *head = result;
        return result;
    }
    abort(); /* Parser validation guarantees an enclosing loop body. */
}

static void patch(code_builder_t *code, instr_index_t head, instr_index_t target) {
    while (head != UINT32_MAX) {
        instruction_t *instruction = get_instruction(code, head);
        instr_index_t next = instruction->arg1;
        instruction->arg1 = target;
        head = next;
    }
}

/** @brief Loop scopes survive continue; all exits destroy them without producing a value. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    for_t *loop = (for_t *)node;
    loop->breaks = loop->continues = UINT32_MAX;
    bool post = node->vtbl->type == NODE_DO_WHILE;
    instr_index_t first = add_instruction(code, (instruction_t){.opcode = LOOP_ENTER});
    generate_bytecode_from_node(get_child(node, 0), code, data);
    instr_index_t head = get_next_instruction_index(code), exit = UINT32_MAX;
    if (!post) {
        generate_bytecode_from_node(get_child(node, 1), code, data);
        exit = add_instruction(code, (instruction_t){.opcode = JIF});
    }
    generate_bytecode_from_node(get_child(node, 3), code, data);
    instr_index_t next = get_next_instruction_index(code);
    generate_bytecode_from_node(get_child(node, 2), code, data);
    if (post) {
        generate_bytecode_from_node(get_child(node, 1), code, data);
        exit = add_instruction(code, (instruction_t){.opcode = JIF});
    }
    add_instruction(code, (instruction_t){.opcode = JUMP, .arg1 = head});
    instr_index_t end = get_next_instruction_index(code);
    get_instruction(code, exit)->arg1 = end;
    patch(code, loop->breaks, end);
    patch(code, loop->continues, node->vtbl->type == NODE_WHILE ? head : next);
    add_instruction(code, (instruction_t){.opcode = RESTORE});
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

node_t *
create_while_node(arena_t *arena, expression_t *condition, statement_t *body, bool post_test) {
    static node_vtbl_t pre_vtbl, post_vtbl;
    pre_vtbl = post_vtbl = for_vtbl;
    pre_vtbl.type = NODE_WHILE;
    pre_vtbl.type_name = L"while";
    post_vtbl.type = NODE_DO_WHILE;
    post_vtbl.type_name = L"do while";
    node_t *node = create_for_node(arena,
                                   create_statement_expression_node(arena, NULL),
                                   condition,
                                   create_statement_expression_node(arena, NULL),
                                   body);
    node->vtbl = post_test ? &post_vtbl : &pre_vtbl;
    return node;
}
