/**
 * @file abstract_state.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the abstract interpreter state.
 *
 * This file implements the abstract-state container used by abstract
 * interpretation.
 *
 * The state maps declaration nodes to small reference-counted records containing
 * two abstract values:
 *
 *     current
 *         The value known at the current program point.
 *
 *     summary
 *         The join of all values assigned to the declaration through this state.
 *
 * The AVL tree owns the pair records through its value copy/destroy callbacks.
 * This allows abstract states to be cloned without duplicating lattice elements.
 */

#include "abstract_state.h"
#include "lattice.h"
#include "lib/allocate.h"
#include "graph/declarations.h"

/**
 * @brief Compares declarator pointers.
 *
 * Provides a strict ordering over declarator addresses so they can be used as
 * AVL tree keys. Declarators are identity objects at this stage: after semantic
 * binding, the pointer itself is the resolved declaration.
 *
 * @param left First declarator pointer.
 * @param right Second declarator pointer.
 * @return -1 if `left` is lower than `right`, +1 if greater, or 0 if equal.
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
 * @struct lattice_pair_t
 * @brief Reference-counted pair of abstract values for one declarator.
 *
 * Each abstract-state entry stores both the current value and the accumulated
 * summary for a declaration.
 *
 * Pair objects may be shared between cloned abstract states. The AVL tree calls
 * `copy_value()` when copying a value and `destroy_value()` when destroying it,
 * so this structure carries a tiny reference counter.
 */
typedef struct {
    /**
     * @brief Number of AVL entries currently referencing this pair.
     */
    int refs;

    /**
     * @brief Abstract value at the current program point.
     *
     * This value is used by expression calculation when reading declarations.
     */
    const lattice_element_t *current;

    /**
     * @brief Accumulated abstract value observed for the declaration.
     *
     * This value is updated with lattice joins and eventually flushed into the
     * corresponding declarator.
     */
    const lattice_element_t *summary;
} lattice_pair_t;

/**
 * @brief Copies an AVL value containing a lattice pair.
 *
 * The pair itself is not duplicated. Instead, its reference counter is
 * incremented and the same pointer is returned.
 *
 * @param value AVL value whose pointer references a lattice pair.
 * @return The same value after incrementing the pair reference count.
 */
static value_t copy_value(value_t value) {
    lattice_pair_t *pair = (lattice_pair_t*)value.ptr;
    pair->refs++;
    return value;
}

/**
 * @brief Destroys an AVL value containing a lattice pair.
 *
 * Decrements the pair reference counter and frees the pair when the last AVL
 * entry releases it.
 *
 * @param value AVL value whose pointer references a lattice pair.
 */
static void destroy_value(value_t value) {
    lattice_pair_t *pair = (lattice_pair_t*)value.ptr;
    if (!(--pair->refs)) {
        FREE(pair);
    }
}

/**
 * @brief Creates an empty abstract state.
 *
 * Allocates a state object and creates its AVL tree. The tree is configured to
 * share lattice-pair values through reference counting when cloned or destroyed.
 *
 * @param arena Arena used later for lattice operations such as joins.
 * @return Pointer to the newly created abstract state.
 */
abstract_state_t *create_abstract_state(arena_t *arena) {
    abstract_state_t *state = (abstract_state_t*)ALLOC(sizeof(abstract_state_t));
    state->arena = arena;
    state->values = create_avl_tree(declarator_comparator);
    state->values->copy_value = copy_value;
    state->values->destroy_value = destroy_value;
    return state;
}

/**
 * @brief Clones an abstract state.
 *
 * The AVL tree is cloned while preserving its shape. Lattice-pair values are
 * shared through the AVL copy callback, which increments their reference counts.
 *
 * @param state Source abstract state.
 * @return Newly allocated clone, or NULL if `state` is NULL.
 */
abstract_state_t *clone_abstract_state(const abstract_state_t *state) {
    if (!state) {
        return NULL;
    }
    abstract_state_t *copy = (abstract_state_t*)ALLOC(sizeof(abstract_state_t));
    copy->arena = state->arena;
    copy->values = clone_avl_tree(state->values);
    return copy;
}

/**
 * @brief Sets the current abstract value for a declarator.
 *
 * If the declarator is already present, its current value is overwritten and
 * its summary is updated by joining the previous summary with the new value.
 * If the declarator is not present, a new lattice-pair record is allocated.
 *
 * @param state Target abstract state.
 * @param declarator Declarator whose value should be updated.
 * @param value New current abstract value.
 * @return Previous current value if the declarator existed, otherwise NULL.
 */
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

/**
 * @brief Gets the current abstract value for a declarator.
 *
 * Looks up the declarator in the state and returns the pair's current value.
 * The summary value is intentionally not returned here.
 *
 * @param state Source abstract state.
 * @param declarator Declarator to look up.
 * @return Current abstract value if present, otherwise NULL.
 */
const lattice_element_t *get_from_abstract_state(const abstract_state_t *state,
        const declarator_t *declarator) {
    lattice_pair_t *pair = (lattice_pair_t*)get_from_avl_tree(
        state->values,
        (void*)declarator
    ).ptr;
    return pair ? pair->current : NULL;
}

/**
 * @brief Checks whether the state contains a declarator entry.
 *
 * @param state Source abstract state.
 * @param declarator Declarator to look up.
 * @return true if the declarator is present, false otherwise.
 */
bool abstract_state_contains(const abstract_state_t *state, const declarator_t *declarator) {
    return avl_tree_contains(state->values, (void*)declarator);
}

/**
 * @brief Flushes one abstract-state entry into its declarator.
 *
 * Stores the accumulated summary value into the declarator's `abstract_value`
 * field. This callback is used by `flush_abstract_state()`.
 *
 * @param user_data Unused callback context.
 * @param key Declarator key.
 * @param value AVL value containing a lattice-pair pointer.
 */
static void flush_abstract_state_entry(void *user_data, void* key, value_t value) {
    declarator_t *declarator = (declarator_t*)key;
    lattice_pair_t *pair = (lattice_pair_t*)value.ptr;
    declarator->abstract_value = pair->summary;
}

/**
 * @brief Writes accumulated summaries from the state into AST declarators.
 *
 * Iterates over all stored declarations and copies each pair's summary value
 * into the corresponding declarator. After this, the AST carries the final
 * declaration-level abstract facts used by graph visualization and later
 * compiler stages.
 *
 * @param state Source abstract state.
 */
void flush_abstract_state(const abstract_state_t *state) {
    avl_tree_for_each(state->values, flush_abstract_state_entry, NULL);
}

/**
 * @brief Destroys an abstract state.
 *
 * Destroys the AVL tree, which releases all stored lattice-pair records through
 * the configured value destroy callback, then frees the state object.
 *
 * @param state Abstract state to destroy.
 */
void destroy_abstract_state(abstract_state_t *state) {
    if (state) {
        destroy_avl_tree(state->values);
        FREE(state);
    }
}