/**
 * @file abstract_state.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Abstract interpreter state: a program-point snapshot of facts about declarations.
 *
 * This module defines the storage used by the abstract interpreter to represent
 * what is known about program variables, constants, parameters, and other named
 * entities at a particular point of execution.
 *
 * The central idea is deliberately simple:
 *
 *     declarator_t* -> const lattice_element_t*
 *
 * A declarator is used as the key because name resolution has already happened
 * before abstract interpretation starts. 
 *
 * An abstract state is therefore not "the value of one variable". It is a whole
 * snapshot of the abstract machine state at one program point.
 */

#pragma once

#include "lib/value.h"
#include "lib/avl_tree.h"

/**
 * @typedef abstract_state_t
 * @brief Forward declaration for the abstract state structure.
 */
typedef struct abstract_state_t abstract_state_t;

/**
 * @typedef declarator_t
 * @brief Forward declaration for the declarator structure.
 */
typedef struct declarator_t declarator_t;

/**
 * @typedef lattice_element_t
 * @brief Forward declaration for the lattice element structure.
 */
typedef struct lattice_element_t lattice_element_t;

struct abstract_state_t {
    arena_t *arena; 
    avl_tree_t *values;
};

abstract_state_t *create_abstract_state(arena_t *arena);

abstract_state_t *clone_abstract_state(const abstract_state_t *state);

const lattice_element_t *set_in_abstract_state(abstract_state_t *state,
        const declarator_t *declarator, const lattice_element_t *value);

const lattice_element_t *get_from_abstract_state(const abstract_state_t *state,
        const declarator_t *declarator);

bool abstract_state_contains(const abstract_state_t *state,
        const declarator_t *declarator);

void flush_abstract_state(const abstract_state_t *state);

void destroy_abstract_state(abstract_state_t *state);
