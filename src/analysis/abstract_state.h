/**
 * @file abstract_state.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Abstract interpreter state: current facts and declaration summaries.
 */

#pragma once

#include "collector.h"
#include "common/control_flow.h"
#include "lib/avl_tree.h"
#include "lib/value.h"

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

    /** @brief Optional borrowed collector, shared by branch states. */
    analysis_collector_t *collector;

    /** @brief Mapping from declarators to lattice-pair records. */
    avl_tree_t *values;

    /** @brief Current abstract control-flow mode. */
    control_flow_t control_flow;

    /** @brief Borrowed return-summary accumulator (NULL/BOTTOM initially); shared across branches.
     */
    const lattice_element_t **return_value;

    /** @brief Borrowed activation and shared evaluation budget. */
    struct abstract_call_frame_t *call_frame;
    size_t *call_budget;

    /** @brief An untracked write may have shadowed root-provided names. */
    bool builtin_bindings_unknown;

    /** @brief Shared failure flag for isolated return-type analysis; NULL during normal analysis.
     */
    bool *type_analysis_incomplete;

    /** @brief Optional caller vertex during call discovery or recursive type solving. */
    struct function_call_graph_node_t *call_graph_node;

    /** @brief Recursive type group being solved; never used by ordinary interpretation. */
    struct recursive_type_group_t *recursive_group;

    /** @brief Optional expression proof accumulator; never used by ordinary interpretation. */
    struct c_expression_context_t *c_expressions;
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
 * Current values and summaries are isolated by copy-on-write; control flow is copied.
 * Declarators, immutable lattice values, the collector, and return-output slot remain borrowed.
 * Either state may be destroyed first; shared arenas must outlive both.
 * @return Newly allocated clone, or NULL if `state` is NULL.
 */
abstract_state_t *clone_abstract_state(const abstract_state_t *state);

/**
 * @brief Sets the current abstract value for a declarator.
 * @return Previous current abstract value if the declarator was already present, otherwise NULL.
 */
const lattice_element_t *set_in_abstract_state(abstract_state_t *state,
                                               const declarator_t *declarator,
                                               const lattice_element_t *value);

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
bool abstract_state_contains(const abstract_state_t *state, const declarator_t *declarator);

/**
 * @brief Joins two abstract states.
 *
 * Joins summaries from both paths; only FLOW_NORMAL paths contribute current values.
 * Missing current entries on continuing paths mean NULL. Keeps declarations from either input.
 * @return Newly allocated joined abstract state, or NULL if either input state is NULL.
 */
abstract_state_t *join_abstract_states(const abstract_state_t *left, const abstract_state_t *right);

/** @brief Writes accumulated summaries from the state into AST declarators. */
void flush_abstract_state(const abstract_state_t *state);

/** @brief Destroys an abstract state. */
void destroy_abstract_state(abstract_state_t *state);

/** @brief Records a write at its source node; the ordinary setter uses the declaration node. */
const lattice_element_t *set_in_abstract_state_at(abstract_state_t *state,
                                                  const declarator_t *declarator,
                                                  const lattice_element_t *value,
                                                  const node_t *node);

/** @brief Records current values after merging branches at node. */
void collect_joined_abstract_state(const abstract_state_t *state, const node_t *node);

/** @brief Invalidates facts after an unknown call. */
void forget_abstract_values(abstract_state_t *state);

/** @brief Imports call effects and summaries, keeping caller-local bindings intact. */
void apply_abstract_call_state(abstract_state_t *caller,
                               const abstract_state_t *result,
                               const node_t *function);

/** @brief Resets locals from an earlier invocation of the same function. */
void reset_abstract_call_locals(abstract_state_t *state, const node_t *function);

/** @brief Invalidates shared captures after self-call, preserving the caller's own locals. */
void forget_captured_abstract_values(abstract_state_t *state, const node_t *function);

/** @brief Joins a loop back edge and widens changing payloads to type domains.
 * Returns a new state; stable means its current facts match the previous head.
 */
abstract_state_t *
widen_loop_state(const abstract_state_t *head, const abstract_state_t *back_edge, bool *stable);
