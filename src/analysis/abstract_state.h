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

/**
 * @struct abstract_state_t
 * @brief Represents an abstract interpreter state.
 *
 * The state maps declarators to lattice elements:
 *
 *     declarator_t* -> const lattice_element_t*
 *
 * It is a snapshot of abstract values known at a particular program point.
 */
struct abstract_state_t {
    /**
     * @brief Mapping from declarators to abstract values.
     *
     * Keys are declarator_t pointers.
     * Values are const lattice_element_t pointers stored through value_t.ptr.
     */
    avl_tree_t *values;
};

/**
 * @brief Creates an empty abstract state.
 *
 * @return Pointer to the newly created abstract state.
 */
abstract_state_t *create_abstract_state();

/**
 * @brief Clones an abstract state.
 *
 * The clone preserves the exact AVL tree shape and therefore runs in O(N).
 * Keys and lattice element pointers are copied shallowly.
 *
 * @param state Source abstract state.
 * @return Newly allocated clone, or NULL if state is NULL.
 */
abstract_state_t *clone_abstract_state(const abstract_state_t *state);

/**
 * @brief Sets the abstract value associated with a declarator.
 *
 * @param state Target abstract state.
 * @param declarator Declarator key.
 * @param value Abstract lattice value.
 * @return Previous abstract value if present; otherwise NULL.
 */
const lattice_element_t *set_in_abstract_state(abstract_state_t *state,
        const declarator_t *declarator, const lattice_element_t *value);

/**
 * @brief Gets the abstract value associated with a declarator.
 *
 * @param state Source abstract state.
 * @param declarator Declarator key.
 * @return Abstract lattice value if present; otherwise NULL.
 */
const lattice_element_t *get_from_abstract_state(const abstract_state_t *state,
        const declarator_t *declarator);

/**
 * @brief Checks whether the abstract state contains a declarator.
 *
 * @param state Source abstract state.
 * @param declarator Declarator key.
 * @return true if declarator exists in the state, false otherwise.
 */
bool abstract_state_contains(const abstract_state_t *state,
        const declarator_t *declarator);

/**
 * @brief Applies a function to each declarator-value pair in the abstract state.
 *
 * @param state Source abstract state.
 * @param func Callback function.
 * @param user_data User data passed to callback.
 */
void abstract_state_for_each(const abstract_state_t *state,
        void (*func)(void *user_data, const declarator_t *declarator,
                const lattice_element_t *value),
        void *user_data);

/**
 * @brief Destroys an abstract state and all its AVL nodes.
 *
 * Does not destroy declarators or lattice elements, only the state container.
 *
 * @param state Abstract state to destroy.
 */
void destroy_abstract_state(abstract_state_t *state);