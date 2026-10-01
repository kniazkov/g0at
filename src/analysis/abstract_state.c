/**
 * @file abstract_state.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the abstract interpreter state.
 */

#include <assert.h>

#include "abstract_state.h"
#include "lattice.h"
#include "lib/allocate.h"
#include "graph/declarations.h"

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
    lattice_pair_t *pair = (lattice_pair_t*)value.ptr;
    pair->refs++;
    return value;
}

/** @brief Destroys an AVL value containing a lattice pair. */
static void destroy_value(value_t value) {
    lattice_pair_t *pair = (lattice_pair_t*)value.ptr;
    if (!(--pair->refs)) {
        FREE(pair);
    }
}

abstract_state_t *create_abstract_state(arena_t *arena) {
    abstract_state_t *state = (abstract_state_t*)ALLOC(sizeof(abstract_state_t));
    state->arena = arena;
    state->values = create_avl_tree(declarator_comparator);
    state->values->copy_value = copy_value;
    state->values->destroy_value = destroy_value;
    state->control_flow = FLOW_NORMAL;
    state->return_value = NULL;
    return state;
}

abstract_state_t *clone_abstract_state(const abstract_state_t *state) {
    if (!state) {
        return NULL;
    }
    abstract_state_t *copy = (abstract_state_t*)ALLOC(sizeof(abstract_state_t));
    copy->arena = state->arena;
    copy->values = clone_avl_tree(state->values);
    copy->control_flow = FLOW_NORMAL;
    copy->return_value = state->return_value;
    return copy;
}

const lattice_element_t *set_in_abstract_state(abstract_state_t *state,
        const declarator_t *declarator, const lattice_element_t *value) {
    lattice_pair_t *pair = (lattice_pair_t*)get_from_avl_tree(
        state->values,
        (void*)declarator
    ).ptr;
    if (pair) {
        const lattice_element_t *old_value = pair->current;
        pair->current = value;
        pair->summary = lattice_join(state->arena, pair->summary, value);
        return old_value;
    } else {
        pair = (lattice_pair_t*)ALLOC(sizeof(lattice_pair_t));
        pair->refs = 0; // will be increased by AVL tree
        pair->current = value;
        pair->summary = value;
        set_in_avl_tree(state->values, (void*)declarator, (value_t){ .ptr = (void*)pair });
        return NULL;
    }
}

const lattice_element_t *get_from_abstract_state(const abstract_state_t *state,
        const declarator_t *declarator) {
    lattice_pair_t *pair = (lattice_pair_t*)get_from_avl_tree(
        state->values,
        (void*)declarator
    ).ptr;
    return pair ? pair->current : NULL;
}

bool abstract_state_contains(const abstract_state_t *state, const declarator_t *declarator) {
    return avl_tree_contains(state->values, (void*)declarator);
}

/** @brief Context passed to the abstract-state join traversal callback. */
typedef struct {
    /** @brief Right-hand state used for matching entries. */
    const abstract_state_t *right;

    /** @brief Result state being populated. */
    abstract_state_t *result;
} join_context_t;

/** @brief Joins one abstract-state entry if it exists in both states. */
static void join_abstract_state_entry(void *user_data, void *key, value_t value) {
    join_context_t *context = (join_context_t*)user_data;
    lattice_pair_t *left_pair = (lattice_pair_t*)value.ptr;
    lattice_pair_t *right_pair = (lattice_pair_t*)get_from_avl_tree(
        context->right->values,
        key
    ).ptr;

    if (!right_pair) {
        return;
    }

    lattice_pair_t *joined_pair = (lattice_pair_t*)ALLOC(sizeof(lattice_pair_t));
    joined_pair->refs = 0;
    joined_pair->current = lattice_join(
        context->result->arena,
        left_pair->current,
        right_pair->current
    );
    joined_pair->summary = lattice_join(
        context->result->arena,
        left_pair->summary,
        right_pair->summary
    );

    set_in_avl_tree(
        context->result->values,
        key,
        (value_t){ .ptr = joined_pair }
    );
}

abstract_state_t *join_abstract_states(const abstract_state_t *left,
        const abstract_state_t *right) {
    if (!left || !right) {
        return NULL;
    }

    assert(left->arena == right->arena);

    abstract_state_t *result = create_abstract_state(left->arena);
    result->control_flow =
        left->control_flow == right->control_flow ? left->control_flow : FLOW_NORMAL;
    result->return_value =
        left->return_value == right->return_value ? left->return_value : NULL;

    join_context_t context = {
        .right = right,
        .result = result
    };

    avl_tree_for_each(left->values, join_abstract_state_entry, &context);
    return result;
}

/** @brief Flushes one abstract-state entry into its declarator. */
static void flush_abstract_state_entry(void *user_data, void* key, value_t value) {
    declarator_t *declarator = (declarator_t*)key;
    lattice_pair_t *pair = (lattice_pair_t*)value.ptr;
    declarator->abstract_value = pair->summary;
}

void flush_abstract_state(const abstract_state_t *state) {
    avl_tree_for_each(state->values, flush_abstract_state_entry, NULL);
}

void destroy_abstract_state(abstract_state_t *state) {
    if (state) {
        destroy_avl_tree(state->values);
        FREE(state);
    }
}
