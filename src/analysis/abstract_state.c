/**
 * @file abstract_state.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the abstract state structure and its operations.
 */

#include "abstract_state.h"
#include "lattice.h"
#include "lib/allocate.h"
#include "graph/declarations.h"

/**
 * @brief Compares declarator pointers.
 *
 * @param left First declarator pointer.
 * @param right Second declarator pointer.
 * @return Negative if left < right, positive if left > right, zero otherwise.
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

typedef struct {
    int refs;
    const lattice_element_t *current;
    const lattice_element_t *summary;
} lattice_pair_t;

static value_t copy_value(value_t value) {
    lattice_pair_t *pair = (lattice_pair_t*)value.ptr;
    pair->refs++;
    return value;
}

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
    return state;
}

abstract_state_t *clone_abstract_state(const abstract_state_t *state) {
    if (!state) {
        return NULL;
    }
    abstract_state_t *copy = (abstract_state_t*)ALLOC(sizeof(abstract_state_t));
    copy->arena = state->arena;
    copy->values = clone_avl_tree(state->values);
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
