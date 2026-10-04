/**
 * @file statement.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definition of the statement structure.
 */

#pragma once

#include "analysis/lattice.h"
#include "node.h"

typedef struct declarator_spec_t declarator_spec_t;
typedef struct statement_list_t statement_list_t;
typedef struct declarator_t declarator_t;

/**
 * @brief The structure representing a statement node.
 *
 * A statement may contain one or more expressions (e.g., the expression `x = 5` is part of a
 * statement), but the statement itself does not return a value.
 */
struct statement_t {
    /** @brief Base node structure, providing common attributes for all nodes. */
    node_t base;
};

/** @brief Gets the primary display data associated with a statement. */
static inline node_display_value_t get_statement_data(const statement_t *stmt) {
    return get_node_data(&stmt->base);
}

/** @brief Gets the number of direct child nodes of a statement. */
static inline size_t get_statement_child_count(const statement_t *stmt) {
    return get_node_child_count(&stmt->base);
}

/**
 * @brief Gets a child node of a statement by index.
 * `index`: Zero-based child index.
 * @return Pointer to the child node or NULL if index is out of range.
 */
static inline node_t *get_statement_child(const statement_t *stmt, size_t index) {
    return get_node_child(&stmt->base, index);
}

/**
 * @brief Gets the tag/label for a child node of a statement.
 * `index`: Zero-based child index.
 * @return Wide character string with the child tag or NULL if not applicable.
 */
static inline const wchar_t *get_statement_child_tag(const statement_t *stmt, size_t index) {
    return get_node_child_tag(&stmt->base, index);
}

/**
 * @brief Executes abstract interpretation for a statement.
 * @return Output abstract state after interpreting this statement.
 */
static inline abstract_state_t *
execute_statement(statement_t *stmt, abstract_state_t *state, arena_t *arena) {
    return execute_node(&stmt->base, state, arena);
}

/** @brief Generates a single-line Goat source code representation from a statement. */
static inline string_value_t generate_goat_code_from_statement(const statement_t *stmt) {
    return generate_goat_code_from_node(&stmt->base);
}

/** @brief Generates indented Goat source code from a statement. */
static inline void generate_indented_goat_code_from_statement(const statement_t *stmt,
                                                              source_builder_t *builder,
                                                              size_t indent) {
    generate_indented_goat_code_from_node(&stmt->base, builder, indent);
}

/** @brief Internal expression lowering within a function specialization. */
static inline c_generated_expression_t
generate_c_code_from_statement(const statement_t *stmt, c_generation_context_t *context) {
    return generate_c_code_from_node(&stmt->base, context);
}

/** @brief Internal statement lowering within a function specialization. */
static inline bool generate_indented_c_code_from_statement(const statement_t *stmt,
                                                           c_generation_context_t *context,
                                                           source_builder_t *builder,
                                                           size_t indent) {
    return generate_indented_c_code_from_node(&stmt->base, context, builder, indent);
}

/** @brief Generates bytecode from a statement. */
static inline instr_index_t
generate_bytecode_from_statement(statement_t *stmt, code_builder_t *code, data_builder_t *data) {
    return generate_bytecode_from_node(&stmt->base, code, data);
}

/**
 * @brief Generates deferred bytecode from a statement.
 * @return `true` if deferred bytecode was successfully generated in this pass; `false` otherwise.
 */
static inline bool generate_deferred_bytecode_from_statement(const statement_t *stmt,
                                                             code_builder_t *code,
                                                             data_builder_t *data) {
    return generate_deferred_bytecode_from_node(&stmt->base, code, data);
}

/** @brief Creates a statement expression; NULL produces an empty statement. */
statement_t *create_statement_expression_node(arena_t *arena, expression_t *wrapped);

/**
 * @brief Creates a return statement node.
 * `value`: Expression to return, or `NULL` for a bare `return;`.
 */
node_t *create_return_node(arena_t *arena, expression_t *value);

/**
 * @brief Creates an if-else statement node.
 * `false_branch`: Statement executed when the condition is false, or NULL when there is no else
 * branch.
 */
node_t *create_if_else_node(arena_t *arena,
                            expression_t *condition,
                            statement_t *true_branch,
                            statement_t *false_branch);

/** @brief Creates a scoped C-style loop; all four children are non-NULL. */
node_t *create_for_node(arena_t *arena,
                        statement_t *initial,
                        expression_t *condition,
                        statement_t *step,
                        statement_t *body);

/** @brief Stores the condition truth proven by the reachability pass. */
void set_if_else_condition_truth(node_t *node, abstract_truth_t truth);

/**
 * @brief Creates `try statement catch (identifier) statement_list`.
 * Copies the nonempty identifier to arena; handler is a block expression.
 * Exceptional abstract interpretation is conservative until path tracking is implemented.
 */
node_t *create_try_catch_node(arena_t *arena,
                              statement_t *body,
                              string_view_t exception_name,
                              statement_list_t *handler);

/** @brief Creates a throw with a required expression. */
node_t *create_throw_node(arena_t *arena, expression_t *value);

/** @brief Gets the catch-local declaration used by name binding. */
declarator_t *get_catch_declarator(node_t *node);
