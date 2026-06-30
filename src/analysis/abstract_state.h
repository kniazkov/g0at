/**
 * @file abstract_state.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Abstract interpreter state: current facts and declaration summaries.
 *
 * This module defines the state container used by the abstract interpreter.
 *
 * Abstract interpretation does not execute a program with concrete runtime
 * values. Instead, it walks the syntax tree and tracks conservative facts about
 * declarations using lattice elements. Those facts may describe constants,
 * intervals, numeric categories, nullability, functions, unknown values, and
 * other abstract properties.
 *
 * The central mapping is:
 *
 *     declarator_t* -> lattice_pair_t
 *
 * A declarator pointer is used as the key because name resolution has already
 * happened before abstract interpretation starts. The interpreter should not
 * repeatedly resolve textual names. It should work with the exact declaration
 * object that a variable usage is bound to. Apparently compilers prefer facts
 * over vibes. Rude, but effective.
 *
 * Each stored value contains two lattice elements:
 *
 *     current
 *         The abstract value known at the current program point. Expression
 *         calculation reads this value when it needs to know what a declaration
 *         means here and now.
 *
 *     summary
 *         The join of all abstract values assigned to the declaration while this
 *         state has been interpreted. This is the declaration's whole observed
 *         value history, useful for deciding whether it can be represented by a
 *         native C type, displayed in the AST graph, or used by later optimizer
 *         stages without replaying the analysis.
 *
 * This separation matters. For example:
 *
 *     var x = 1;
 *     x = 2;
 *     print(x);
 *
 * At the call to `print`, the current value of `x` is `2`, but the summary for
 * `x` is the join of `1` and `2`. The former is useful for local precision and
 * constant folding. The latter is useful for declaration-level representation
 * and code generation decisions.
 *
 * The abstract state owns its AVL tree and the small lattice-pair containers
 * stored inside it. Lattice elements themselves are not owned by the state; they
 * are expected to live in the arena passed to `create_abstract_state()`.
 *
 * Abstract states are heap-allocated, cloneable, and destroyable.
 */

#pragma once

#include "lib/value.h"
#include "lib/avl_tree.h"

/**
 * @typedef abstract_state_t
 * @brief Forward declaration for the abstract interpreter state.
 */
typedef struct abstract_state_t abstract_state_t;

/**
 * @typedef declarator_t
 * @brief Forward declaration for declaration AST nodes.
 *
 * Declarators are used as stable identity keys in the abstract state.
 */
typedef struct declarator_t declarator_t;

/**
 * @typedef lattice_element_t
 * @brief Forward declaration for abstract-value lattice elements.
 *
 * Lattice elements represent abstract facts such as constants, intervals,
 * numeric categories, nullability, top, bottom, and similar analysis results.
 */
typedef struct lattice_element_t lattice_element_t;

/**
 * @enum abstract_control_flow_t
 * @brief Control-flow mode of the abstract interpreter.
 *
 * Describes whether interpretation should continue normally or whether a
 * return statement has already transferred control out of the current function.
 */
typedef enum {
    /**
     * @brief Normal execution flow.
     *
     * The interpreter continues executing statements in the current function.
     */
    FLOW_NORMAL,

    /**
     * @brief Return flow.
     *
     * A return statement has been executed and the interpreter should unwind the
     * current function body without executing following statements.
     */
    FLOW_RETURN
} abstract_control_flow_t;

/**
 * @struct abstract_state_t
 * @brief Mutable abstract state used during abstract interpretation.
 *
 * The state stores abstract facts for declarations at a specific point of
 * analysis. Internally, each declarator maps to a pair of lattice elements:
 * one for the current program point and one accumulated summary.
 *
 * The state owns the AVL tree and its pair objects. It does not own declarators
 * or lattice elements.
 */
struct abstract_state_t {
    /**
     * @brief Arena used for allocating derived lattice elements.
     *
     * The state does not own the arena. It is used when new lattice elements are
     * produced while updating summaries, for example by `lattice_join()`.
     */
    arena_t *arena;

    /**
     * @brief Mapping from declarators to lattice-pair records.
     *
     * Keys are `declarator_t*`.
     * Values are internal reference-counted lattice-pair objects stored through
     * `value_t.ptr`.
     */
    avl_tree_t *values;

    /**
     * @brief Current abstract control-flow mode.
     *
     * Indicates whether interpretation should continue normally or whether a
     * return statement has already been encountered in the current function.
     */
    abstract_control_flow_t control_flow;

    /**
     * @brief Abstract value returned from the current function.
     *
     * Stores a pointer to the lattice-element pointer produced by a return
     * statement.
     */
    const lattice_element_t **return_value;
};

/**
 * @brief Creates an empty abstract state.
 *
 * Allocates the state and its AVL tree using the regular heap allocator. The
 * provided arena is stored for later lattice operations, but is not owned by the
 * state.
 *
 * @param arena Arena used for allocating lattice elements produced by state
 *        updates.
 * @return Pointer to the newly created abstract state.
 */
abstract_state_t *create_abstract_state(arena_t *arena);

/**
 * @brief Clones an abstract state.
 *
 * Creates a new state with a cloned AVL tree. Stored lattice-pair records are
 * shared through reference counting by the AVL value-copy callback, while
 * declarator keys and lattice elements remain shallow references.
 *
 * @param state Source abstract state.
 * @return Newly allocated clone, or NULL if `state` is NULL.
 */
abstract_state_t *clone_abstract_state(const abstract_state_t *state);

/**
 * @brief Sets the current abstract value for a declarator.
 *
 * Updates the current value associated with the given declarator. The summary
 * value is also updated by joining the previous summary with the new value.
 *
 * If the declarator is not yet present in the state, a new lattice-pair record
 * is created where both current and summary initially point to `value`.
 *
 * @param state Target abstract state.
 * @param declarator Declarator whose abstract value is being updated.
 * @param value New current abstract value.
 * @return Previous current abstract value if the declarator was already present,
 *         otherwise NULL.
 */
const lattice_element_t *set_in_abstract_state(abstract_state_t *state,
        const declarator_t *declarator, const lattice_element_t *value);

/**
 * @brief Gets the current abstract value for a declarator.
 *
 * Reads the program-point value associated with the declarator. This function
 * does not return the accumulated summary.
 *
 * @param state Source abstract state.
 * @param declarator Declarator to look up.
 * @return Current abstract value if present, otherwise NULL.
 */
const lattice_element_t *get_from_abstract_state(const abstract_state_t *state,
        const declarator_t *declarator);

/**
 * @brief Checks whether the state contains a declarator entry.
 *
 * @param state Source abstract state.
 * @param declarator Declarator to look up.
 * @return `true` if the declarator exists in the state, `false` otherwise.
 */
bool abstract_state_contains(const abstract_state_t *state,
        const declarator_t *declarator);

/**
 * @brief Writes accumulated summaries from the state into AST declarators.
 *
 * Iterates over all entries in the state and stores each pair's summary lattice
 * element into the corresponding declarator's `abstract_value` field.
 *
 * This is the bridge between the temporary abstract interpreter state and the
 * persistent AST facts used by visualization, optimization, bytecode generation,
 * and future native-code generation.
 *
 * @param state Source abstract state.
 */
void flush_abstract_state(const abstract_state_t *state);

/**
 * @brief Destroys an abstract state.
 *
 * Frees the AVL tree, releases reference-counted lattice-pair records through
 * the tree's destroy callback, and frees the state object itself.
 *
 * Declarators, lattice elements, and the arena are not destroyed.
 *
 * @param state Abstract state to destroy.
 */
void destroy_abstract_state(abstract_state_t *state);
