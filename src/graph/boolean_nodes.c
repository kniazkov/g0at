/**
 * @file boolean_nodes.c
 * © 2025 Ivan Kniazkov
 * @brief Implementation of the boolean literal expressions (`true` and `false`).
 *
 * This file defines the behavior of the boolean literal expressions, which represent
 * the logical constants `true` and `false` in the syntax tree. Each value is modeled
 * as a singleton node (no per-node state) and can be used in variable initializers,
 * assignments, conditions, and function returns.
 */

#include "expression.h"
#include "common_methods.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"

/**
 * @struct boolean_true_t
 * @brief Represents a boolean literal `true` expression node.
 *
 * The `true` value is a singleton logical constant. The structure extends
 * `expression_t` but contains no additional fields as no extra storage is needed.
 */
typedef struct {
    /**
     * @brief Base expression structure from which boolean_true_t inherits.
     */
    expression_t base;
} boolean_true_t;

/**
 * @struct boolean_false_t
 * @brief Represents a boolean literal `false` expression node.
 *
 * The `false` value is a singleton logical constant. The structure extends
 * `expression_t` but contains no additional fields as no extra storage is needed.
 */
typedef struct {
    /**
     * @brief Base expression structure from which boolean_false_t inherits.
     */
    expression_t base;
} boolean_false_t;

/**
 * @brief Converts a boolean `true` expression to its string representation.
 *
 * This function returns the string "true" as the representation of the boolean
 * `true` literal, matching how it would appear in source code. The returned
 * string is a constant value and doesn't require memory management.
 *
 * @param node A pointer to the `true` expression node (unused).
 * @return A `string_value_t` containing the literal string "true".
 */
static string_value_t generate_goat_code_true(const node_t *node) {
    return STATIC_STRING(L"true");
}

/**
 * @brief Generates indented Goat source code for a boolean `true` expression.
 *
 * @param node Pointer to the AST node representing the boolean `true` expression.
 * @param builder Pointer to the source builder where generated code will be stored.
 * @param indent The current indentation level (in tabs) for code generation (unused).
 */
static void generate_indented_goat_code_true(const node_t *node, source_builder_t *builder,
        size_t indent) {
    append_static_source(builder, L"true");
}

/**
 * @brief Generates bytecode for a boolean `true` expression.
 *
 * This function emits the `TRUE` opcode. The `TRUE` instruction pushes the boolean
 * true value onto the virtual machine's stack when executed.
 *
 * @param node A pointer to the node representing the boolean `true` expression (unused).
 * @param code A pointer to the `code_builder_t` structure used for generating instructions.
 * @param data A pointer to the `data_builder_t` structure (unused in this case).
 * @return The instruction index of the first emitted instruction.
 */
static instr_index_t generate_bytecode_true(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    return add_instruction(code, (instruction_t){ .opcode = TRUE });
}

/**
 * @brief Virtual table for boolean `true` expressions.
 *
 * This virtual table provides the implementation of operations specific to the
 * boolean `true` literal.
 */
static node_vtbl_t true_vtbl = {
    .type = NODE_TRUE,
    .type_name = L"true",
    .get_data = no_data,
    .get_child_count = no_children,
    .get_child = no_child,
    .get_child_tag = no_tags,
    .generate_goat_code = generate_goat_code_true,
    .generate_indented_goat_code = generate_indented_goat_code_true,
    .generate_bytecode = generate_bytecode_true,
};

/**
 * @brief Singleton instance of the boolean `true` expression node.
 *
 * Since `true` is a constant value with no state, we use a single shared instance
 * rather than creating new nodes for each occurrence in the syntax tree.
 */
static boolean_true_t true_instance = {
    .base = {
        .base = {
            .vtbl = &true_vtbl
        }
    }
};

/**
 * @brief Converts a boolean `false` expression to its string representation.
 *
 * This function returns the string "false" as the representation of the boolean
 * `false` literal, matching how it would appear in source code. The returned
 * string is a constant value and doesn't require memory management.
 *
 * @param node A pointer to the `false` expression node (unused).
 * @return A `string_value_t` containing the literal string "false".
 */
static string_value_t generate_goat_code_false(const node_t *node) {
    return STATIC_STRING(L"false");
}

/**
 * @brief Generates indented Goat source code for a boolean `false` expression.
 *
 * @param node Pointer to the AST node representing the boolean `false` expression.
 * @param builder Pointer to the source builder where generated code will be stored.
 * @param indent The current indentation level (in tabs) for code generation (unused).
 */
static void generate_indented_goat_code_false(const node_t *node, source_builder_t *builder,
        size_t indent) {
    append_static_source(builder, L"false");
}

/**
 * @brief Generates bytecode for a boolean `false` expression.
 *
 * This function emits the `FALSE` opcode. The `FALSE` instruction pushes the boolean
 * false value onto the virtual machine's stack when executed.
 *
 * @param node A pointer to the node representing the boolean `false` expression (unused).
 * @param code A pointer to the `code_builder_t` structure used for generating instructions.
 * @param data A pointer to the `data_builder_t` structure (unused in this case).
 * @return The instruction index of the first emitted instruction.
 */
static instr_index_t generate_bytecode_false(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    return add_instruction(code, (instruction_t){ .opcode = FALSE });
}

/**
 * @brief Virtual table for boolean `false` expressions.
 *
 * This virtual table provides the implementation of operations specific to the
 * boolean `false` literal.
 */
static node_vtbl_t false_vtbl = {
    .type = NODE_FALSE,
    .type_name = L"false",
    .get_data = no_data,
    .get_child_count = no_children,
    .get_child = no_child,
    .get_child_tag = no_tags,
    .generate_goat_code = generate_goat_code_false,
    .generate_indented_goat_code = generate_indented_goat_code_false,
    .generate_bytecode = generate_bytecode_false,
};

/**
 * @brief Singleton instance of the boolean `false` expression node.
 *
 * Since `false` is a constant value with no state, we use a single shared instance
 * rather than creating new nodes for each occurrence in the syntax tree.
 */
static boolean_false_t false_instance = {
    .base = {
        .base = {
            .vtbl = &false_vtbl
        }
    }
};

node_t *get_true_node_instance() {
    return &true_instance.base.base;
}

node_t *get_false_node_instance() {
    return &false_instance.base.base;
}
