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

#include <assert.h>

#include "expression.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "codegen/code_builder.h"

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
    for (size_t index = 0; index < expr->args_count; index++) {
        if (index > 0) {
            append_substring(&builder, L", ", 2);
        }
        expression_t *arg = expr->args[index];
        string_value_t arg_as_string = arg->base.vtbl->to_string(&arg->base);
        append_substring(&builder, arg_as_string.data, arg_as_string.length);
        if (arg_as_string.should_free) {
            FREE(arg_as_string.data);
        }        
    }

    return append_char(&builder, L')');
}

/**
 * @brief Generates bytecode for a function call expression.
 * 
 * This function generates the bytecode for a function call expression. It processes the arguments
 * of the function call first, generating the bytecode for each argument by iterating through the
 * list of arguments in reverse order. Then, it generates the bytecode for the function object
 * itself. Finally, it adds a `CALL` instruction to the bytecode, with the argument count.
 * 
 * @param node A pointer to the function call node in the abstract syntax tree.
 * @param code A pointer to the `code_builder_t` structure, which is used to build the instructions.
 * @param data A pointer to the `data_builder_t` used to manage static data.
 * 
 * @note This function assumes that the number of arguments for the function call does not exceed
 *  `UINT16_MAX` (the 16-bit unsigned integer limit).
 */
static void gen_bytecode_for_function_call(const node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const function_call_t *expr = (const function_call_t *)node;
    assert(expr->args_count < UINT16_MAX);
    if (expr->args_count > 0) {
        size_t index = expr->args_count;
        do {
            index--;
            expression_t *arg = expr->args[index];
            arg->base.vtbl->gen_bytecode(&arg->base, code, data);
        } while (index > 0);
    }
    expr->func_object->base.vtbl->gen_bytecode(&expr->func_object->base, code, data);
    add_instruction(code, (instruction_t){ .opcode = CALL, .arg0 = (uint16_t)expr->args_count });
}

/**
 * @brief Virtual table for function call expressions.
 * 
 * This virtual table provides the implementation of operations specific to function call
 * expressions within the abstract syntax tree (AST). Function calls in the AST consist of a
 * function object and its arguments, and this virtual table defines how to handle those
 * specific operations.
 * 
 * The table includes the following function pointers:
 * - `to_string`: Converts the function call node to its string representation.
 * - `gen_bytecode`: Generates the bytecode for the function call, including the bytecode for
 *   each argument and the function object itself, followed by the `CALL` instruction.
 */
static node_vtbl_t function_call_vtbl = {
    .type = NODE_FUNCTION_CALL,
    .to_string = function_call_to_string,
    .gen_bytecode = gen_bytecode_for_function_call,
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
