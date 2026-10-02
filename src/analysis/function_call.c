/** @file function_call.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Bounded, context-sensitive interpretation of function calls.
 */
#include "function_call.h"

#include "graph/declarations.h"
#include "graph/node.h"

/** @brief Arena-owned identity; exit states live only while the invocation is active. */
typedef struct abstract_call_frame_t {
    struct abstract_call_frame_t *parent;
    node_t *function;
    abstract_state_t *exits;
} abstract_call_frame_t;

void collect_abstract_return(abstract_state_t *state) {
    abstract_call_frame_t *frame = state->call_frame;
    if (!frame)
        return;
    abstract_state_t *exit = clone_abstract_state(state);
    exit->control_flow = FLOW_NORMAL;
    if (frame->exits) {
        abstract_state_t *merged = join_abstract_states(frame->exits, exit);
        destroy_abstract_state(frame->exits);
        destroy_abstract_state(exit);
        exit = merged;
    }
    frame->exits = exit;
}

/** @brief Exception handlers need exceptional return-state modelling before precise calls. */
static bool has_unmodeled_flow(node_t *node) {
    if (node->vtbl->type == NODE_TRY_CATCH)
        return true;
    if (node->vtbl->type == NODE_FUNCTION_OBJECT)
        return false;
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        if (has_unmodeled_flow(get_node_child(node, i)))
            return true;
    }
    return false;
}

const lattice_element_t *interpret_function_call(const lattice_element_t *function,
                                                 const lattice_element_t *const *args,
                                                 size_t count,
                                                 abstract_state_t *caller) {
    if (function->type != LATTICE_KNOWN_FUNCTION) {
        if (function->type == LATTICE_TOP || function->type == LATTICE_NOT_NULL
            || function->type == LATTICE_FUNCTION)
            forget_abstract_values(caller);
        return make_top_element();
    }
    const known_function_element_t *known = (const known_function_element_t *)function;
    bool owner_active = known->owner == NULL;
    bool recursive = false;
    size_t depth = 0;
    for (abstract_call_frame_t *p = caller->call_frame; p; p = p->parent) {
        owner_active |= p == known->owner;
        recursive |= p->function == known->node;
        depth++;
    }
    node_t *body = get_node_child(known->node, 1);
    if (!owner_active || recursive || depth >= 32 || !*caller->call_budget
        || has_unmodeled_flow(body)) {
        forget_abstract_values(caller);
        return make_top_element();
    }
    --*caller->call_budget;
    abstract_call_frame_t *frame = alloc_zeroed_from_arena(caller->arena, sizeof(*frame));
    frame->parent = caller->call_frame;
    frame->function = known->node;
    abstract_state_t *state = clone_abstract_state(caller);
    reset_abstract_call_locals(state, known->node);
    state->call_frame = frame;
    const lattice_element_t *result = make_bottom_element();
    state->return_value = &result;
    node_t *parameters = get_node_child(known->node, 0);
    for (size_t i = 0; i < get_node_child_count(parameters); i++) {
        set_in_abstract_state(state,
                              (declarator_t *)get_node_child(parameters, i),
                              i < count ? args[i] : make_null_element());
    }
    for (size_t i = 0; i < get_node_child_count(body) && state->control_flow == FLOW_NORMAL; i++)
        state = execute_node(get_node_child(body, i), state, caller->arena);
    if (state->control_flow == FLOW_NORMAL) {
        result = lattice_join(caller->arena, result, make_null_element());
        collect_abstract_return(state);
    }
    if (frame->exits) {
        /* Keep observations from terminated paths without importing their current facts. */
        state->control_flow = FLOW_UNREACHABLE;
        abstract_state_t *merged = join_abstract_states(state, frame->exits);
        apply_abstract_call_state(caller, merged, known->node);
        destroy_abstract_state(merged);
        destroy_abstract_state(frame->exits);
        frame->exits = NULL;
    } else {
        /* No normal exit: retain observed summaries, but stop the caller's path. */
        apply_abstract_call_state(caller, state, known->node);
        caller->control_flow = FLOW_UNREACHABLE;
    }
    destroy_abstract_state(state);
    return result;
}
