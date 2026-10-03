/**
 * @file expression.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definition of the expression structure.
 */

#pragma once

#include "analysis/abstract_state.h"
#include "analysis/c_expression.h"
#include "analysis/lattice.h"
#include "data_type.h"
#include "node.h"

typedef struct declarator_spec_t declarator_spec_t;
typedef struct function_summary_set_t function_summary_set_t;

/** @brief The structure representing an expression node. */
struct expression_t {
    /** @brief Base node structure, providing common attributes for all nodes. */
    node_t base;

    /**
     * @brief The semantic data type of this expression (optional).
     *
     * May be NULL for untyped/invalid expressions during early parsing or when the type is
     * inferred/unknown at the current stage.
     */
    const data_type_t *data_type;

    /** @brief Pointwise value from immediate reachability; never a call specialization. */
    const lattice_element_t *immediate_value;
};

/** @brief Gets the primary display data associated with an expression. */
static inline node_display_value_t get_expression_data(const expression_t *expr) {
    return get_node_data(&expr->base);
}

/** @brief Gets the number of direct child nodes of an expression. */
static inline size_t get_expression_child_count(const expression_t *expr) {
    return get_node_child_count(&expr->base);
}

/**
 * @brief Gets a child node of an expression by index.
 * `index`: Zero-based child index.
 * @return Pointer to the child node or NULL if index is out of range.
 */
static inline node_t *get_expression_child(const expression_t *expr, size_t index) {
    return get_node_child(&expr->base, index);
}

/**
 * @brief Gets the tag/label for a child node of an expression.
 * `index`: Zero-based child index.
 * @return Wide character string with the child tag or NULL if not applicable.
 */
static inline const wchar_t *get_expression_child_tag(const expression_t *expr, size_t index) {
    return get_node_child_tag(&expr->base, index);
}

/** @brief Calculates the abstract lattice element represented by an expression. */
static inline const lattice_element_t *
calculate_expression(expression_t *expr, abstract_state_t *state, arena_t *arena) {
    if (state->control_flow != FLOW_NORMAL)
        return make_bottom_element();
    const lattice_element_t *value = calculate_node(&expr->base, state, arena);
    record_c_expression(state->c_expressions, &expr->base, value);
    if (value->type == LATTICE_BOTTOM && state->control_flow == FLOW_NORMAL) {
        state->control_flow = FLOW_UNREACHABLE;
    }
    return value;
}

/** @brief Generates a single-line Goat source code representation from an expression. */
static inline string_value_t generate_goat_code_from_expression(const expression_t *expr) {
    return generate_goat_code_from_node(&expr->base);
}

/** @brief Generates indented Goat source code from an expression. */
static inline void generate_indented_goat_code_from_expression(const expression_t *expr,
                                                               source_builder_t *builder,
                                                               size_t indent) {
    generate_indented_goat_code_from_node(&expr->base, builder, indent);
}

/** @brief Internal expression lowering within a function specialization. */
static inline c_generated_expression_t
generate_c_code_from_expression(const expression_t *expr, c_generation_context_t *context) {
    return generate_c_code_from_node(&expr->base, context);
}

/** @brief Internal statement lowering within a function specialization. */
static inline bool generate_indented_c_code_from_expression(const expression_t *expr,
                                                            c_generation_context_t *context,
                                                            source_builder_t *builder,
                                                            size_t indent) {
    return generate_indented_c_code_from_node(&expr->base, context, builder, indent);
}

/** @brief Generates bytecode from an expression. */
static inline instr_index_t
generate_bytecode_from_expression(expression_t *expr, code_builder_t *code, data_builder_t *data) {
    return generate_bytecode_from_node(&expr->base, code, data);
}

/** @brief Generates bytecode for storing a value into an expression. */
static inline instr_index_t generate_bytecode_assign_from_expression(const expression_t *expr,
                                                                     code_builder_t *code,
                                                                     data_builder_t *data) {
    return generate_bytecode_assign_from_node(&expr->base, code, data);
}

/**
 * @brief Generates deferred bytecode from an expression.
 * @return `true` if deferred bytecode was successfully generated in this pass; `false` otherwise.
 */
static inline bool generate_deferred_bytecode_from_expression(const expression_t *expr,
                                                              code_builder_t *code,
                                                              data_builder_t *data) {
    return generate_deferred_bytecode_from_node(&expr->base, code, data);
}

/**
 * @brief Creates a new static string expression node.
 * `length`: The length of the string (excluding null terminator).
 */
node_t *create_static_string_node(arena_t *arena, const wchar_t *data, size_t length);

/** @brief Creates a new integer literal node. */
node_t *create_integer_node(arena_t *arena, int64_t value);

/** @brief Creates a real number literal expression node. */
node_t *create_real_number_node(arena_t *arena, double value);

/** @brief Creates a new variable expression node. */
expression_t *create_variable_node(arena_t *arena, string_view_t name);

/**
 * @brief Creates a declarator from an existing variable expression.
 * @warning The returned declarator is heap-allocated and must be freed by the caller when no longer
 * needed.
 * @note The created declarator will have no initializer (NULL), which is valid for variable
 * declarations but invalid for constant declarations.
 */
declarator_spec_t *create_declarator_from_variable(const node_t *expr);

/**
 * @brief Creates a function call expression node with empty arguments.
 * @note The arguments must be set later using set_function_call_arguments()
 */
node_t *create_function_call_node_without_args(arena_t *arena, expression_t *func_object);

/**
 * @brief Sets arguments for a previously created function call node.
 * @pre The node must be of function call type (`NODE_FUNCTION_CALL`)
 * @pre The node must not have arguments already set
 */
void set_function_call_arguments(node_t *node,
                                 arena_t *arena,
                                 expression_t **args,
                                 size_t args_count);

/**
 * @brief Creates a function object node in the AST.
 *
 * The function body is not set here and must be filled later using `fill_function_body`.
 */
node_t *create_function_object_node(arena_t *arena, string_view_t *arg_list, size_t arg_count);

/** @brief Fills in the body of a function object node. */
void fill_function_body(node_t *node, list_t *statements);

/** @brief Gets the arena-owned signature set of a function object. */
function_summary_set_t *get_function_summaries(const node_t *node);

/** @brief Creates a new parenthesized expression node with no inner expression. */
node_t *create_parenthesized_expression_node(arena_t *arena);

/**
 * @brief Fills the inner expression of a parenthesized expression node.
 *
 * Assigns the wrapped expression after node creation.
 */
void fill_parenthesized_expression(node_t *node, expression_t *inner);

/**
 * @brief Creates a new null literal expression node.
 * @return Pointer to the created null literal node.
 */
node_t *create_null_node(arena_t *arena);

/**
 * @brief Creates a new boolean `true` literal expression node.
 * @return Pointer to the created `true` literal node.
 */
node_t *create_true_node(arena_t *arena);

/**
 * @brief Creates a new boolean `false` literal expression node.
 * @return Pointer to the created `false` literal node.
 */
node_t *create_false_node(arena_t *arena);
