/**
 * @file abstract_state.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the abstract interpreter state.
 */

#include "abstract_state.h"

#include "graph/declarations.h"
#include "lattice.h"
#include "lib/allocate.h"

#include <assert.h>

/**
 * @brief Compares declarator pointers.
 *
 * Declarators are identity objects at this stage: after semantic binding, the pointer itself is the
 * resolved declaration.
 */
static int declarator_comparator(const void *left, const void *right) {
    if (left < right) {
        return -1;
    }
    if (left > right) {
        return +1;
    }
    return 0;
}

/**
 * @brief Reference-counted pair of abstract values for one declarator.
 *
 * Pair objects may be shared between cloned abstract states.
 */
typedef struct {
    /** @brief Number of AVL entries currently referencing this pair. */
    int refs;

    /** @brief Abstract value at the current program point. */
    const lattice_element_t *current;

    /** @brief Accumulated abstract value observed for the declaration. */
    const lattice_element_t *summary;
} lattice_pair_t;

/**
 * @brief Copies an AVL value containing a lattice pair.
 * @return The same value after incrementing the pair reference count.
 */
static value_t copy_value(value_t value) {
    lattice_pair_t *pair = (lattice_pair_t *)value.ptr;
    pair->refs++;
    return value;
}

/** @brief Destroys an AVL value containing a lattice pair. */
static void destroy_value(value_t value) {
    lattice_pair_t *pair = (lattice_pair_t *)value.ptr;
    if (!(--pair->refs)) {
        FREE(pair);
    }
}

abstract_state_t *create_abstract_state(arena_t *arena) {
    abstract_state_t *state = (abstract_state_t *)ALLOC(sizeof(abstract_state_t));
    state->arena = arena;
    state->collector = NULL;
    state->values = create_avl_tree(declarator_comparator);
    state->values->copy_value = copy_value;
    state->values->destroy_value = destroy_value;
    state->control_flow = FLOW_NORMAL;
    state->return_value = NULL;
    state->call_frame = NULL;
    state->builtin_bindings_unknown = false;
    state->type_analysis_incomplete = NULL;
    state->call_budget = alloc_from_arena(arena, sizeof(size_t));
    *state->call_budget = 1024;
    return state;
}

abstract_state_t *clone_abstract_state(const abstract_state_t *state) {
    if (!state) {
        return NULL;
    }
    abstract_state_t *copy = (abstract_state_t *)ALLOC(sizeof(abstract_state_t));
    copy->arena = state->arena;
    copy->collector = state->collector;
    copy->values = clone_avl_tree(state->values);
    copy->control_flow = state->control_flow;
    copy->return_value = state->return_value;
    copy->call_frame = state->call_frame;
    copy->builtin_bindings_unknown = state->builtin_bindings_unknown;
    copy->call_budget = state->call_budget;
    copy->type_analysis_incomplete = state->type_analysis_incomplete;
    return copy;
}

const lattice_element_t *set_in_abstract_state(abstract_state_t *state,
                                               const declarator_t *declarator,
                                               const lattice_element_t *value) {
    return set_in_abstract_state_at(state, declarator, value, &declarator->base);
}

const lattice_element_t *set_in_abstract_state_at(abstract_state_t *state,
                                                  const declarator_t *declarator,
                                                  const lattice_element_t *value,
                                                  const node_t *node) {
    if (declarator == get_builtin_declarator())
        state->builtin_bindings_unknown = true;
    add_analysis_event(state->collector, ANALYSIS_VALUE_WRITE, node, declarator, value);
    lattice_pair_t *pair =
        (lattice_pair_t *)get_from_avl_tree(state->values, (void *)declarator).ptr;
    if (pair) {
        const lattice_element_t *old_value = pair->current;
        if (pair->refs > 1) {
            lattice_pair_t *private_pair = ALLOC(sizeof(*private_pair));
            *private_pair = *pair;
            private_pair->refs = 0; // set_in_avl_tree takes the new reference
            set_in_avl_tree(state->values, (void *)declarator, (value_t){.ptr = private_pair});
            pair = private_pair;
        }
        pair->current = value;
        pair->summary = lattice_join(state->arena, pair->summary, value);
        return old_value;
    } else {
        pair = (lattice_pair_t *)ALLOC(sizeof(lattice_pair_t));
        pair->refs = 0; // will be increased by AVL tree
        pair->current = value;
        pair->summary = value;
        set_in_avl_tree(state->values, (void *)declarator, (value_t){.ptr = (void *)pair});
        return NULL;
    }
}

const lattice_element_t *get_from_abstract_state(const abstract_state_t *state,
                                                 const declarator_t *declarator) {
    lattice_pair_t *pair =
        (lattice_pair_t *)get_from_avl_tree(state->values, (void *)declarator).ptr;
    return pair ? pair->current : NULL;
}

bool abstract_state_contains(const abstract_state_t *state, const declarator_t *declarator) {
    return avl_tree_contains(state->values, (void *)declarator);
}

/** @brief Joins facts while only continuing paths contribute current values. */
typedef struct {
    const abstract_state_t *left;
    const abstract_state_t *right;
    abstract_state_t *result;
} join_context_t;

static void join_abstract_state_entry(void *user_data, void *key, value_t ignored) {
    join_context_t *context = user_data;
    if (abstract_state_contains(context->result, key))
        return;
    const lattice_pair_t *left = get_from_avl_tree(context->left->values, key).ptr;
    const lattice_pair_t *right = get_from_avl_tree(context->right->values, key).ptr;
    const lattice_element_t *a = context->left->control_flow == FLOW_NORMAL
                                     ? (left ? left->current : make_null_element())
                                     : make_bottom_element();
    const lattice_element_t *b = context->right->control_flow == FLOW_NORMAL
                                     ? (right ? right->current : make_null_element())
                                     : make_bottom_element();
    lattice_pair_t *pair = ALLOC(sizeof(*pair));
    pair->refs = 0;
    pair->current = lattice_join(context->result->arena, a, b);
    pair->summary = lattice_join(context->result->arena,
                                 left ? left->summary : make_bottom_element(),
                                 right ? right->summary : make_bottom_element());
    set_in_avl_tree(context->result->values, key, (value_t){.ptr = pair});
}

abstract_state_t *join_abstract_states(const abstract_state_t *left,
                                       const abstract_state_t *right) {
    if (!left || !right)
        return NULL;
    assert(left->arena == right->arena);
    assert(left->collector == right->collector);
    abstract_state_t *result = create_abstract_state(left->arena);
    result->collector = left->collector;
    result->control_flow = left->control_flow == FLOW_NORMAL || right->control_flow == FLOW_NORMAL
                               ? FLOW_NORMAL
                           : left->control_flow == FLOW_RETURN || right->control_flow == FLOW_RETURN
                               ? FLOW_RETURN
                               : FLOW_UNREACHABLE;
    result->return_value = left->return_value == right->return_value ? left->return_value : NULL;
    result->call_frame = left->call_frame;
    result->builtin_bindings_unknown =
        left->builtin_bindings_unknown || right->builtin_bindings_unknown;
    result->call_budget = left->call_budget;
    result->type_analysis_incomplete = left->type_analysis_incomplete;
    join_context_t context = {left, right, result};
    avl_tree_for_each(left->values, join_abstract_state_entry, &context);
    avl_tree_for_each(right->values, join_abstract_state_entry, &context);
    return result;
}

/** @brief Flushes one abstract-state entry into its declarator. */
static void flush_abstract_state_entry(void *user_data, void *key, value_t value) {
    declarator_t *declarator = (declarator_t *)key;
    lattice_pair_t *pair = (lattice_pair_t *)value.ptr;
    declarator->abstract_value = pair->summary;
    const abstract_state_t *state = user_data;
    add_analysis_event(state->collector,
                       ANALYSIS_DECLARATION_SUMMARY,
                       &declarator->base,
                       declarator,
                       pair->summary);
}

void flush_abstract_state(const abstract_state_t *state) {
    avl_tree_for_each(state->values, flush_abstract_state_entry, (void *)state);
}

void destroy_abstract_state(abstract_state_t *state) {
    if (state) {
        destroy_avl_tree(state->values);
        FREE(state);
    }
}

typedef struct {
    analysis_collector_t *collector;
    const node_t *node;
} collection_context_t;

static void collect_joined_entry(void *user_data, void *key, value_t value) {
    collection_context_t *context = user_data;
    lattice_pair_t *pair = (lattice_pair_t *)value.ptr;
    add_analysis_event(context->collector, ANALYSIS_STATE_JOIN, context->node, key, pair->current);
}

void collect_joined_abstract_state(const abstract_state_t *state, const node_t *node) {
    if (state->collector) {
        collection_context_t context = {state->collector, node};
        avl_tree_for_each(state->values, collect_joined_entry, &context);
    }
}

/** @brief Finds the function that owns a declaration, excluding nested functions. */
static const node_t *local_owner(const node_t *node) {
    while (node && node->vtbl->type != NODE_FUNCTION_OBJECT)
        node = node->parent;
    return node;
}

static void forget_entry(void *context, void *key, value_t ignored) {
    set_in_abstract_state(context, key, make_top_element());
}

void forget_abstract_values(abstract_state_t *state) {
    state->builtin_bindings_unknown = true;
    avl_tree_for_each(state->values, forget_entry, state);
}

typedef struct {
    abstract_state_t *caller;
    const node_t *function;
} call_copy_t;

/** @brief Copies effects without exposing callee locals as live caller bindings. */
static void copy_call_entry(void *context, void *key, value_t value) {
    call_copy_t *copy = context;
    lattice_pair_t *source = value.ptr;
    lattice_pair_t *old = get_from_avl_tree(copy->caller->values, key).ptr;
    lattice_pair_t *pair = ALLOC(sizeof(*pair));
    pair->refs = 0;
    pair->current = local_owner(key) == copy->function ? (old ? old->current : make_null_element())
                                                       : source->current;
    pair->summary = lattice_join(copy->caller->arena,
                                 old ? old->summary : make_bottom_element(),
                                 source->summary);
    set_in_avl_tree(copy->caller->values, key, (value_t){.ptr = pair});
}

void apply_abstract_call_state(abstract_state_t *caller,
                               const abstract_state_t *result,
                               const node_t *function) {
    caller->builtin_bindings_unknown |= result->builtin_bindings_unknown;
    call_copy_t copy = {caller, function};
    avl_tree_for_each(result->values, copy_call_entry, &copy);
}

static void reset_local(void *context, void *key, value_t value) {
    call_copy_t *copy = context;
    if (local_owner(key) == copy->function) {
        lattice_pair_t *old = value.ptr;
        lattice_pair_t *pair = ALLOC(sizeof(*pair));
        *pair = (lattice_pair_t){0, make_null_element(), old->summary};
        set_in_avl_tree(copy->caller->values, key, (value_t){.ptr = pair});
    }
}

void reset_abstract_call_locals(abstract_state_t *state, const node_t *function) {
    call_copy_t copy = {state, function};
    avl_tree_for_each(state->values, reset_local, &copy);
}
