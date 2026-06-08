/**
 * @file statement.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definition of the statement structure.
 * 
 * This file defines the `statement_t` structure, which represents a statement node in the
 * abstract syntax tree (AST). Unlike an expression, which is a value-producing construct,
 * a statement represents an action or operation that performs something.
 * 
 * Statements do not produce a value for further computation. They are used to control the flow of
 * execution, make assignments, or invoke operations. For example, `x = 5;` or `print("Hello");`
 * are statements.
 * 
 * A statement may contain expressions (such as `x = 5;` where `5` is an expression), but it itself
 * does not evaluate to a value.
 * 
 * In contrast, an expression produces a value, like the result of `5 + 10`.
 */

#pragma once

#include "node.h"

/**
 * @brief Forward declaration of declarator structure (needed for variable & constant declaration).
 */
typedef struct declarator_spec_t declarator_spec_t;

/**
 * @struct statement_t
 * @brief The structure representing a statement node.
 * 
 * A statement is a construct that performs some action in the program.
 * Examples include assignments, function calls, loops, and conditionals. Unlike expressions,
 * statements do not produce values that can be used in further computations. They are evaluated
 * for their side effects.
 * 
 * A statement may contain one or more expressions (e.g., the expression `x = 5` is part of
 * a statement), but the statement itself does not return a value.
 */
struct statement_t {
    /**
     * @brief Base node structure, providing common attributes for all nodes.
     * 
     * The `statement_t` structure inherits from `node_t`, which provides
     * common attributes such as node type and virtual method table.
     */
    node_t base;
};

/**
 * @brief Gets the primary string data associated with a statement.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param stmt A pointer to the statement.
 * @return A `string_value_t` containing the statement data or empty value if none.
 */
static inline string_value_t get_statement_data(const statement_t *stmt) {
    return get_node_data(&stmt->base);
}

/**
 * @brief Gets the number of properties exposed by a statement.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param stmt A pointer to the statement.
 * @return Number of properties exposed by the statement.
 */
static inline size_t get_statement_property_count(const statement_t *stmt) {
    return get_node_property_count(&stmt->base);
}

/**
 * @brief Retrieves a property of a statement by index.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param stmt A pointer to the statement.
 * @param index Zero-based property index.
 * @param out_key Output pointer to receive the property key.
 * @param out_value Output pointer to receive the property value.
 */
static inline void get_statement_property(const statement_t *stmt, size_t index,
        string_view_t *out_key, string_value_t *out_value) {
    get_node_property(&stmt->base, index, out_key, out_value);
}

/**
 * @brief Gets the number of direct child nodes of a statement.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param stmt A pointer to the statement.
 * @return Number of child nodes.
 */
static inline size_t get_statement_child_count(const statement_t *stmt) {
    return get_node_child_count(&stmt->base);
}

/**
 * @brief Gets a child node of a statement by index.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param stmt A pointer to the statement.
 * @param index Zero-based child index.
 * @return Pointer to the child node or NULL if index is out of range.
 */
static inline node_t *get_statement_child(const statement_t *stmt, size_t index) {
    return get_node_child(&stmt->base, index);
}

/**
 * @brief Gets the tag/label for a child node of a statement.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param stmt A pointer to the statement.
 * @param index Zero-based child index.
 * @return Wide character string with the child tag or NULL if not applicable.
 */
static inline const wchar_t *get_statement_child_tag(const statement_t *stmt, size_t index) {
    return get_node_child_tag(&stmt->base, index);
}

/**
 * @brief Executes abstract interpretation for a statement.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param stmt A pointer to the statement.
 * @param state Input abstract state.
 * @return Output abstract state after interpreting this statement.
 */
static inline abstract_state_t *execute_statement(statement_t *stmt, abstract_state_t *state) {
    return execute_node(&stmt->base, state);
}

/**
 * @brief Generates a single-line Goat source code representation from a statement.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param stmt A pointer to the statement.
 * @return A `string_value_t` containing the generated Goat code.
 */
static inline string_value_t generate_goat_code_from_statement(const statement_t *stmt) {
    return generate_goat_code_from_node(&stmt->base);
}

/**
 * @brief Generates indented Goat source code from a statement.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param stmt A pointer to the statement.
 * @param builder A pointer to the source builder.
 * @param indent The number of tabs used for indentation.
 */
static inline void generate_indented_goat_code_from_statement(const statement_t *stmt,
        source_builder_t *builder, size_t indent) {
    generate_indented_goat_code_from_node(&stmt->base, builder, indent);
}

/**
 * @brief Checks whether C code can be generated from a statement.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param stmt A pointer to the statement.
 * @return `true` if C code generation is supported, `false` otherwise.
 */
static inline bool can_generate_c_code_from_statement(const statement_t *stmt) {
    return can_generate_c_code_from_node(&stmt->base);
}

/**
 * @brief Generates a single-line C source code representation from a statement.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param stmt A pointer to the statement.
 * @return A `string_value_t` containing the generated C code or NULL string if
 *  conversion is not possible.
 */
static inline string_value_t generate_c_code_from_statement(const statement_t *stmt) {
    return generate_c_code_from_node(&stmt->base);
}

/**
 * @brief Generates indented C source code from a statement.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param stmt A pointer to the statement.
 * @param builder A pointer to the source builder.
 * @param indent The number of tabs used for indentation.
 */
static inline void generate_indented_c_code_from_statement(const statement_t *stmt,
        source_builder_t *builder, size_t indent) {
    generate_indented_c_code_from_node(&stmt->base, builder, indent);
}

/**
 * @brief Generates bytecode from a statement.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param stmt A pointer to the statement.
 * @param code A pointer to the code builder used for instruction emission.
 * @param data A pointer to the data builder used for static data management.
 * @return The instruction index of the first emitted instruction.
 */
static inline instr_index_t generate_bytecode_from_statement(statement_t *stmt,
        code_builder_t *code, data_builder_t *data) {
    return generate_bytecode_from_node(&stmt->base, code, data);
}

/**
 * @brief Generates deferred bytecode from a statement.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param stmt A pointer to the statement.
 * @param code A pointer to the code builder.
 * @param data A pointer to the data builder.
 * @return `true` if deferred bytecode was successfully generated in this pass;
 *  `false` otherwise.
 */
static inline bool generate_deferred_bytecode_from_statement(const statement_t *stmt,
        code_builder_t *code, data_builder_t *data) {
    return generate_deferred_bytecode_from_node(&stmt->base, code, data);
}

/**
 * @brief Creates a new statement expression node.
 * 
 * This function allocates memory for a new statement expression node, wraps the
 * provided expression, and returns a pointer to the newly created node.
 * 
 * @param arena A pointer to the memory arena for memory allocation.
 * @param wrapped The expression to wrap in the statement expression.
 * @return A pointer to the newly created statement expression node.
 */
statement_t *create_statement_expression_node(arena_t *arena, expression_t *wrapped);

/**
 * @brief Creates a return statement node.
 * 
 * Allocates and initializes a `return_t` node in the given memory arena.
 * The return statement may optionally include a value expression to be returned.
 * 
 * @param arena Memory arena used for allocation.
 * @param value Expression to return, or `NULL` for a bare `return;`.
 * @return A pointer to the created return statement node.
 */
node_t *create_return_node(arena_t *arena, expression_t *value);
