/**
 * @file abstract_state.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Abstract interpreter state: current facts and declaration summaries.
 */

#pragma once

#include "lib/value.h"
#include "lib/avl_tree.h"
#include "common/control_flow.h"

typedef struct abstract_state_t abstract_state_t;

typedef struct declarator_t declarator_t;

typedef struct lattice_element_t lattice_element_t;

/**
 * @brief Mutable abstract state used during abstract interpretation.
 *
 * The state owns the AVL tree and its pair objects. It does not own declarators or lattice
 * elements.
 */
struct abstract_state_t {
    /**
     * @brief Arena used for allocating derived lattice elements.
     *
     * The state does not own the arena.
     */
    arena_t *arena;

    /** @brief Mapping from declarators to lattice-pair records. */
    avl_tree_t *values;

    /** @brief Current abstract control-flow mode. */
    control_flow_t control_flow;

    /** @brief Abstract value returned from the current function. */
    const lattice_element_t **return_value;
};

/**
 * @brief Creates an empty abstract state.
 *
 * The provided arena is stored for later lattice operations, but is not owned by the state.
 */
abstract_state_t *create_abstract_state(arena_t *arena);

/**
 * @brief Clones an abstract state.
 *
 * Stored lattice-pair records are shared through reference counting by the AVL value-copy callback,
 * while declarator keys and lattice elements remain shallow references.
 * @return Newly allocated clone, or NULL if `state` is NULL.
 */
abstract_state_t *clone_abstract_state(const abstract_state_t *state);

/**
 * @brief Sets the current abstract value for a declarator.
 * @return Previous current abstract value if the declarator was already present, otherwise NULL.
 */
const lattice_element_t *set_in_abstract_state(abstract_state_t *state,
        const declarator_t *declarator, const lattice_element_t *value);

/**
 * @brief Gets the current abstract value for a declarator.
 *
 * Does not return the accumulated summary.
 * @return Current abstract value if present, otherwise NULL.
 */
const lattice_element_t *get_from_abstract_state(const abstract_state_t *state,
        const declarator_t *declarator);

/**
 * @brief Checks whether the state contains a declarator entry.
 * @return `true` if the declarator exists in the state, `false` otherwise.
 */
bool abstract_state_contains(const abstract_state_t *state,
        const declarator_t *declarator);

/**
 * @brief Joins two abstract states.
 *
 * For each shared declaration, the resulting entry contains pairwise joins of both the current
 * value and the accumulated summary value.
 * @return Newly allocated joined abstract state, or NULL if either input state is NULL.
 */
abstract_state_t *join_abstract_states(const abstract_state_t *left,
        const abstract_state_t *right);

/** @brief Writes accumulated summaries from the state into AST declarators. */
void flush_abstract_state(const abstract_state_t *state);

/** @brief Destroys an abstract state. */
void destroy_abstract_state(abstract_state_t *state);
