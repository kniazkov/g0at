/**
 * @file function_object.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of function object expressions.
 * 
 * This file defines the behavior of function object expressions in the abstract syntax tree (AST).
 * A function object consists of a parameter list (identifiers) and a function body.
 * When evaluated, it produces a callable function value capturing the current context.
 */

 #include <assert.h>

#include "common_methods.h"
#include "expression.h"
#include "statement.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"

/**
 * @struct function_object_t
 * @brief Represents a function object expression in the AST.
 * 
 * This structure defines a function expression node that encapsulates a list
 * of parameter names and a function body. When evaluated, it produces a function
 * value that can be called with arguments.
 */
typedef struct {
    /**
     * @brief Base expression structure from which function_object_t inherits.
     * 
     * This allows the node to be treated as an expression in the AST while
     * providing the necessary functionality for tree traversal and manipulation.
     */
    expression_t base;

    /**
     * @brief List of function parameter names.
     * 
     * Each entry is a string view representing a parameter identifier.
     */
    string_view_t *arg_list;

    /**
     * @brief Number of function parameters.
     */
    size_t arg_count;

    /**
     * @brief List of statements forming the function body.
     * 
     * This is an array of pointers to statement nodes that make up the function's logic.
     */
    statement_t **stmt_list;

    /**
     * @brief Number of statements in the function body.
     */
    size_t stmt_count;
} function_object_t;

/**
 * @brief Retrieves a string representation of the function's parameter list.
 *
 * This implementation of the `get_data` method for function object nodes returns
 * a comma-separated list of parameter names. If the function has no parameters,
 * an empty string is returned.
 *
 * @param node Pointer to the function object node.
 * @return A `string_value_t` containing the list of parameter names as a string.
 */
static string_value_t get_data(const node_t *node) {
    const function_object_t *expr = (const function_object_t *)node;
    string_value_t result = EMPTY_STRING_VALUE;
    string_builder_t builder;
    init_string_builder(&builder, 0);
    for (size_t index = 0; index < expr->arg_count; index++) {
        if (index > 0) {
            append_static_string(&builder, L", ");
        }
        result = append_string_view(&builder, expr->arg_list[index]);
    }
    return result;
}
