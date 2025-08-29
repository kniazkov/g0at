/**
 * @file parenthesized_expression.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of parenthesized expression node.
 *
 * This file defines the behavior of a parenthesized expression, which is a special case
 * of expression that wraps another expression inside parentheses.
 * The parentheses do not change the semantics of the inner expression but may affect
 * evaluation order and parsing. At creation time, the inner expression is not yet known
 * and is filled later by a separate function.
 */

#include <assert.h>

#include "expression.h"
#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "codegen/source_builder.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"

/**
 * @struct parenthesized_expression_t
 * @brief Represents a parenthesized expression node.
 *
 * This structure defines an expression node that wraps another expression
 * in parentheses. The inner expression is evaluated normally, but the
 * parentheses affect grouping in source code.
 */
typedef struct {
    /**
     * @brief Base expression structure.
     *
     * Inherits from expression_t so the node can be treated as an expression
     * in all contexts where an expression is expected.
     */
    expression_t base;

    /**
     * @brief The wrapped inner expression.
     *
     * May be NULL immediately after creation. It is set later using
     * a dedicated filling function.
     */
    expression_t *inner;
} parenthesized_expression_t;

/**
 * @brief Gets the number of child nodes.
 * @param node Pointer to the parenthesized expression node.
 * @return Always returns 1.
 */
static size_t get_child_count(const node_t *node) {
    return 1;
}

/**
 * @brief Retrieves the child node at the given index.
 * @param node Pointer to the parenthesized expression node.
 * @param index Must be 0 to retrieve the inner expression.
 * @return Pointer to the inner expression node, or NULL if index != 0.
 */
static const node_t* get_child(const node_t *node, size_t index) {
    const parenthesized_expression_t *expr = (const parenthesized_expression_t *)node;
    if (index == 0) {
        return &expr->inner->base;
    }
    return NULL;
}

/**
 * @brief Gets the child tag for the given index.
 * @param node Pointer to the node (unused).
 * @param index Must be 0.
 * @return Static wide string "expression" or NULL if index != 0.
 */
static const wchar_t* get_child_tag(const node_t *node, size_t index) {
    return (index == 0) ? L"expression" : NULL;
}

/**
 * @brief Converts the parenthesized expression to its string representation.
 *
 * This function generates the string representation of the parenthesized expression
 * by surrounding the string representation of the inner expression with parentheses.
 *
 * @param node A pointer to the parenthesized expression node.
 * @return A `string_value_t` containing the formatted string representation.
 */
static string_value_t generate_goat_code(const node_t *node) {
    const parenthesized_expression_t *expr = (const parenthesized_expression_t *)node;
    string_value_t inner_str = expr->inner->base.vtbl->generate_goat_code(&expr->inner->base);
    string_builder_t sb;
    init_string_builder(&sb, inner_str.length + 2); // +2 for parentheses
    append_char(&sb, L'(');
    append_string_value(&sb, inner_str);
    string_value_t result = append_char(&sb, L')');
    FREE_STRING(inner_str);
    return result;
}

/**
 * @brief Generates indented Goat source code for the parenthesized expression.
 *
 * Produces formatted Goat source code with proper indentation,
 * enclosing the inner expression in parentheses.
 *
 * @param node A pointer to the node.
 * @param builder A pointer to the `source_builder_t`.
 * @param indent The number of tabs used for indentation.
 */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
        size_t indent) {
    const parenthesized_expression_t *expr = (const parenthesized_expression_t *)node;
    append_static_source(builder, L"(");
    expr->inner->base.vtbl->generate_indented_goat_code(&expr->inner->base, builder, 0);
    append_static_source(builder, L")");
}

/**
 * @brief Generates bytecode for a parenthesized expression node.
 *
 * This function generates bytecode for the wrapped inner expression directly.
 * No additional instructions are required, since parentheses only affect grouping.
 *
 * @param node A pointer to the node representing a parenthesized expression.
 * @param code A pointer to the `code_builder_t`.
 * @param data A pointer to the `data_builder_t`.
 * @return The instruction index of the first emitted instruction.
 */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    parenthesized_expression_t *expr = (parenthesized_expression_t *)node;
    return expr->inner->base.vtbl->generate_bytecode(&expr->inner->base, code, data);
}

/**
 * @brief Virtual table for parenthesized expression operations.
 */
static node_vtbl_t expression_parenthesized_vtbl = {
    .type = NODE_EXPRESSION_PARENTHESIZED,
    .type_name = L"parenthesized expression",
    .get_data = no_data,
    .get_child_count = get_child_count,
    .get_child = get_child,
    .get_child_tag = get_child_tag,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
};

node_t *create_parenthesized_expression_node(arena_t *arena) {
    parenthesized_expression_t *expr =
        (parenthesized_expression_t *)alloc_from_arena(arena, sizeof(parenthesized_expression_t));
    expr->base.base.vtbl = &expression_parenthesized_vtbl;
    expr->inner = NULL;
    return &expr->base.base;
}

void fill_parenthesized_expression(node_t *node, expression_t *inner) {
    assert(node->vtbl->type == NODE_EXPRESSION_PARENTHESIZED);
    parenthesized_expression_t *expr = (parenthesized_expression_t *)node;
    expr->inner = inner;
}
