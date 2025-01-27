/**
 * @file function_call.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of function call expressions.
 * 
 * This file defines the behavior of the function call expression, which represents
 * a function call in the abstract syntax tree (AST). A function call consists of
 * a function object (e.g., the function being called) and a list of arguments.
 * The arguments are evaluated and passed to the function when invoked.
 */

#include "expression.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"

/**
 * @struct function_call_t
 * @brief Represents a function call expression node.
 * 
 * This structure defines a function call expression in the AST. It includes
 * a reference to the function object being called and a list of arguments passed
 * to the function.
 */
typedef struct {
    /**
     * @brief Base expression structure from which function_call_t inherits.
     * 
     * This structure allows the function call node to be treated polymorphically
     * as an `expression_t`, enabling operations such as printing and evaluation.
     */
    expression_t base;

    /**
     * @brief The function object being called.
     * 
     * This is the expression representing the function being invoked. It could be a variable,
     * a function literal, or some other form of callable object.
     */
    expression_t *func_object;

    /**
     * @brief The arguments passed to the function.
     * 
     * This array stores the expressions representing the arguments to the function.
     * Each argument is evaluated and passed to the function when the call is made.
     */
    expression_t **args;

    /**
     * @brief The number of arguments in the function call.
     * 
     * This value indicates how many arguments are in the `args` array.
     */
    size_t args_count;
} function_call_t;

/**
 * @brief Converts a function call expression to its string representation.
 * 
 * This function converts the given function call expression to a string that represents how the
 * call would appear in source code. It includes the function name and the arguments
 * in the correct syntax.
 * 
 * @param node A pointer to the function call expression node.
 * @return A `string_value_t` containing the formatted string representation of
 *  the function call, including the function name and arguments.
 */
static string_value_t function_call_to_string(const node_t *node) {
    const function_call_t *expr = (const function_call_t *)node;
    string_builder_t builder;
    init_string_builder(&builder, 0);

    string_value_t func_object_as_string =
        expr->func_object->base.vtbl->to_string(&expr->func_object->base);
    append_substring(&builder, func_object_as_string.data, func_object_as_string.length);
    if (func_object_as_string.should_free) {
        FREE(func_object_as_string.data);
    }

    append_char(&builder, L'(');
    for (size_t i = 0; i < expr->args_count; i++) {
        if (i > 0) {
            append_substring(&builder, L", ", 2);
        }
        expression_t *arg = expr->args[i];
        string_value_t arg_as_string = arg->base.vtbl->to_string(&arg->base);
        append_substring(&builder, arg_as_string.data, arg_as_string.length);
        if (arg_as_string.should_free) {
            FREE(arg_as_string.data);
        }        
    }

    return append_char(&builder, L')');
}

/**
 * @brief Converts a node representing a function call into a statement.
 * @param node A pointer to the node to be converted.
 * @return `NULL` since function calls cannot be treated as statements.
 */
static statement_t *function_call_to_statement(node_t *node) {
    return NULL;
}

/**
 * @brief Converts a node representing a function call into an expression.
 * @param node A pointer to the node to be converted.
 * @return The node casted as an `expression_t*`.
 */
static expression_t *function_call_to_expression(node_t *node) {
    return (expression_t *)node;
}

/**
 * @brief Virtual table for function call expressions.
 * 
 * This virtual table provides the implementation of operations specific to function call
 * expressions. It includes function pointers for common operations such as:
 * - Converting the expression to its string representation.
 * - Casting the function call to a statement (currently returns `NULL`).
 * - Casting the function call to an expression (returns the node as an expression).
 * 
 * This virtual table allows function call nodes to handle specific behavior for the
 * function call type within the abstract syntax tree.
 */
static node_vtbl_t function_call_vtbl = {
    .type = NODE_FUNCTION_CALL,
    .to_string = function_call_to_string,
    .to_statement = function_call_to_statement,
    .to_expression = function_call_to_expression
};

node_t *create_function_call_node(arena_t *arena, expression_t *func_object, expression_t **args,
        size_t args_count) {
    function_call_t *expr = (function_call_t *)alloc_from_arena(arena, sizeof(function_call_t));
    expr->base.base.vtbl = &function_call_vtbl;
    expr->func_object = func_object;
    size_t data_size = args_count * sizeof(expression_t *);
    expr->args = (expression_t **)alloc_from_arena(arena, data_size);
    memcpy(expr->args, args, data_size);
    expr->args_count = args_count;
    return &expr->base.base;
}
