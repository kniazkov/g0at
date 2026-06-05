/**
 * @file abstract_state.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the abstract state structure and its operations.
 */

#include "abstract_state.h"
#include "lib/allocate.h"

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
        return 1;
    }
    return 0;
}

/**
 * @brief Adapter context for abstract_state_for_each().
 */
typedef struct {
    void (*func)(void *user_data, const declarator_t *declarator,
            const lattice_element_t *value);
    void *user_data;
} abstract_state_for_each_context_t;

/**
 * @brief Adapter from AVL callback signature to abstract state callback signature.
 *
 * @param user_data Pointer to abstract_state_for_each_context_t.
 * @param key Declarator key.
 * @param value Lattice element value wrapped in value_t.
 */
static void abstract_state_for_each_adapter(void *user_data, void *key, value_t value) {
    abstract_state_for_each_context_t *context =
        (abstract_state_for_each_context_t*)user_data;

    context->func(
        context->user_data,
        (const declarator_t*)key,
        (const lattice_element_t*)value.ptr
    );
}

abstract_state_t *create_abstract_state() {
    abstract_state_t *state = (abstract_state_t*)ALLOC(sizeof(abstract_state_t));
    state->values = create_avl_tree(declarator_comparator);
    return state;
}

abstract_state_t *clone_abstract_state(const abstract_state_t *state) {
    if (!state) {
        return NULL;
    }

    abstract_state_t *copy = (abstract_state_t*)ALLOC(sizeof(abstract_state_t));
    copy->values = clone_avl_tree(state->values);

    return copy;
}

const lattice_element_t *set_in_abstract_state(abstract_state_t *state,
        const declarator_t *declarator, const lattice_element_t *value) {
    return (const lattice_element_t*)set_in_avl_tree(
        state->values,
        (void*)declarator,
        (value_t){.ptr = (void*)value}
    ).ptr;
}

const lattice_element_t *get_from_abstract_state(const abstract_state_t *state,
        const declarator_t *declarator) {
    return (const lattice_element_t*)get_from_avl_tree(
        state->values,
        (void*)declarator
    ).ptr;
}

bool abstract_state_contains(const abstract_state_t *state,
        const declarator_t *declarator) {
    return avl_tree_contains(state->values, declarator);
}

void abstract_state_for_each(const abstract_state_t *state,
        void (*func)(void *user_data, const declarator_t *declarator,
                const lattice_element_t *value),
        void *user_data) {
    if (!state || !func) {
        return;
    }

    abstract_state_for_each_context_t context = {
        .func = func,
        .user_data = user_data
    };

    avl_tree_for_each(state->values, abstract_state_for_each_adapter, &context);
}

void destroy_abstract_state(abstract_state_t *state) {
    if (state) {
        destroy_avl_tree(state->values);
        FREE(state);
    }
}
