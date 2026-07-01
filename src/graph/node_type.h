/**
 * @file node_type.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Enumeration of node types for the abstract syntax tree (AST).
 *
 * This file defines the @ref node_type_t enumeration, which lists all possible
 * node types used in the abstract syntax tree (AST). Each node type corresponds
 * to a specific language construct.
 */

#pragma once

#include <stdbool.h>

/**
 * @enum node_type_t
 * @brief Enumeration of node types in the abstract syntax tree (AST).
 * 
 * Each node type represents a category of nodes in the AST, which correspond to different
 * language constructs (e.g., expressions, statements, literals). The node type is used to
 * uniquely identify the kind of information stored within the node and guide operations like
 * parsing, traversal, and code generation.
 * 
 * Types are used in the AST to determine the appropriate handling and processing for different
 * kinds of language constructs.
 */
typedef enum {
    /**
     * @brief Root node type.
     *
     * This node type represents the root of the abstract syntax tree (AST). It serves as the
     * entry point to the AST, containing the main structure of the parsed source code.
     */
    NODE_ROOT = 0,

    /**
     * @brief Function argument list node type.
     *
     * Represents a container for formal function arguments.
     */
    NODE_ARGUMENT_LIST,

    /**
     * @brief Function body node type.
     *
     * Represents a brace-delimited list of statements used as a function body.
     * Unlike a regular statement list, it does not create an additional lexical
     * environment during bytecode generation.
     */
    NODE_FUNCTION_BODY,

    /*
        ----------------------------------------------------------------------
        DECLARATOR NODE TYPES.
    
        Declarators are syntax tree nodes that introduce a single named entity.
        Each declarator declares one variable, constant, or formal function argument.

        Declaration statement nodes may contain multiple declarators, but each
        declarator node represents exactly one declared name.
    */

    /**
     * @brief Function argument node type.
     *
     * Represents a single formal function argument.
     */
    NODE_ARGUMENT,

    /**
     * @brief Variable declarator node type.
     *
     * Represents a single variable declarator in a declaration statement.
     * Contains:
     * - Identifier node (required)
     * - Initializer expression node (optional)
     */
    NODE_VARIABLE_DECLARATOR,

    /**
     * @brief Constant declarator node type.
     *
     * Represents a single constant declarator in a declaration statement.
     * Contains:
     * - Identifier node (required)
     * - Initializer expression node (required)
     */
    NODE_CONSTANT_DECLARATOR,

    /*  
        ----------------------------------------------------------------------
        EXPRESSION NODE TYPES.
      
        Expressions are syntax tree nodes that represent entities producing a value.
        Every expression can be evaluated and has a resulting value, even if that
        value is later ignored by an enclosing statement.
     */

    /**
     * @brief Statement list node type.
     *
     * This node represents a list of statements. This list has its own scope.
     */
    NODE_STATEMENT_LIST,

    /**
     * @brief Null literal node type.
     *
     * This node type represents a null literal in the source code, indicating the absence
     * of a value. The node contains no additional data as null is a singleton value.
     */
    NODE_NULL,

    /**
     * @brief Boolean literal `true` node type.
     *
     * This node type represents the boolean constant `true` in the source code.
     * The node contains no additional data as the value is a singleton that can be
     * referenced directly.
     */
    NODE_TRUE,

    /**
     * @brief Boolean literal `false` node type.
     *
     * This node type represents the boolean constant `false` in the source code.
     * The node contains no additional data as the value is a singleton that can be
     * referenced directly.
     */
    NODE_FALSE,

    /**
     * @brief Static string node type.
     *
     * This node type represents a static string literal in the source code.
     * It stores the string content as a part of the AST.
     */
    NODE_STATIC_STRING,

    /**
     * @brief Integer literal node type.
     *
     * This node type represents a 64-bit integer literal in the source code.
     * It stores the integer value as part of the AST.
     */
    NODE_INTEGER,

    /**
     * @brief Real number node type.
     *
     * This node type represents a 64-bit real number in the source code.
     * It stores the real value as part of the AST.
     */
    NODE_REAL,

    /**
     * @brief Variable node type.
     *
     * This node type represents a variable expression in the source code.
     * It stores the name of the variable, and can be used to represent variables
     * or other named entities (e.g., constants).
     */
    NODE_VARIABLE,

    /**
     * @brief Parenthesized expression node type.
     *
     * This node type represents an expression wrapped in parentheses in the source code.
     * It stores a single inner expression. Parentheses do not change the semantics of the
     * expression itself, but they enforce explicit grouping and can affect evaluation order
     * during parsing and code generation.
     */
    NODE_EXPRESSION_PARENTHESIZED,

    /**
     * @brief Function object expression node type.
     *
     * Represents a function object consisting of a parameter list and a body.
     * When evaluated, it produces a callable function capturing its lexical scope.
     */
    NODE_FUNCTION_OBJECT,

    /**
     * @brief Function call node type.
     *
     * This node type represents a function call expression in the source code.
     * It stores information about the function being called, including the function object
     * expression and the arguments passed to the function.
     */
    NODE_FUNCTION_CALL,

    /**
     * @brief Simple assignment operation node type.
     *
     * This node type represents a simple assignment operation in the source code.
     * It stores the left-hand side (target variable) and right-hand side (assigned value).
     */
    NODE_SIMPLE_ASSIGNMENT,

    /**
     * @brief Addition operation node type.
     *
     * This node type represents a binary addition (`+`) operation in the source code.
     * It stores the left and right operands of the addition.
     */
    NODE_ADDITION,

    /**
     * @brief Subtraction operation node type.
     *
     * This node type represents a binary subtraction (`-`) operation in the source code.
     * It stores the left and right operands of the subtraction.
     */
    NODE_SUBTRACTION,

    /**
     * @brief Multiplication operation node type.
     *
     * This node type represents a binary multiplication operation (`*`) in the source code.
     * It stores the left and right operands of the multiplication.
     */
    NODE_MULTIPLICATION,

    /**
     * @brief Division operation node type.
     *
     * This node type represents a binary division operation (`/`) in the source code.
     * It stores the left and right operands of the division.
     */
    NODE_DIVISION,

    /**
     * @brief Modulo (remainder) operation node type.
     *
     * This node type represents a binary modulo operation (`%`) in the source code.
     * It stores the left and right operands of the operation.
     */
    NODE_MODULO,

    /**
     * @brief Exponentiation operation node type.
     *
     * This node type represents a binary power operation (`**` or `^`) in the source code.
     * It stores the base (left operand) and the exponent (right operand).
     */
    NODE_POWER,

    /**
     * @brief Less-than comparison node type.
     *
     * Represents the `<` operation between two operands.
     */
    NODE_LESS,

    /**
     * @brief Less-than-or-equal comparison node type.
     *
     * Represents the `<=` operation between two operands.
     */
    NODE_LESS_OR_EQUAL,

    /**
     * @brief Greater-than comparison node type.
     *
     * Represents the `>` operation between two operands.
     */
    NODE_GREATER,

    /**
     * @brief Greater-than-or-equal comparison node type.
     *
     * Represents the `>=` operation between two operands.
     */
    NODE_GREATER_OR_EQUAL,

    /**
     * @brief Equality comparison node type.
     *
     * Represents the `==` operation between two operands.
     */
    NODE_EQUAL,

    /**
     * @brief Inequality comparison node type.
     *
     * Represents the `!=` operation between two operands.
     */
    NODE_NOT_EQUAL,

    /*
        ----------------------------------------------------------------------
        STATEMENT NODE TYPES
    
        Statements are syntax tree nodes that represent instructions executed
        sequentially. A statement may evaluate expressions internally, but the
        statement itself is used for control flow, declarations, or side effects
        rather than for producing a value.
    */

    /**
     * @brief Statement expression node type.
     *
     * This node type represents a statement expression, which contains an expression
     * whose result is evaluated but ignored. It is used to represent statements
     * where an expression is evaluated solely for its side effects, such as a function call.
     * The result of the expression is not used further in the program.
     */
    NODE_STATEMENT_EXPRESSION,

    /**
     * @brief Variable declaration statement node type.
     *
     * This node represents a variable declaration statement (e.g., "var x = 1, y, z = 2 + 3").
     * Contains child nodes of type NODE_VARIABLE_DECLARATOR for each declared variable.
     * The node itself serves as a container and doesn't store specific values.
     */
    NODE_VARIABLE_DECLARATION,

    /**
     * @brief Constant declaration statement node type.
     *
     * This node represents a constant declaration statement (e.g., "const pi = 3.14").
     * Contains child nodes of type NODE_CONSTANT_DECLARATOR for each declared constant.
     * The node itself serves as a container and doesn't store specific values.
     */
    NODE_CONSTANT_DECLARATION,

    /**
     * @brief Return statement node type.
     *
     * Represents a `return` statement in the syntax tree. May optionally include
     * a value expression to be returned from the function.
     * Contains:
     * - Return value expression node (optional)
     */
    NODE_RETURN,

    /**
     * @brief Conditional branch statement node type.
     *
     * Represents an `if` statement in the syntax tree, optionally followed by an
     * `else` branch.
     * Contains:
     * - Condition expression node
     * - Statement node executed when the condition is true
     * - Statement node executed when the condition is false (optional)
     */
    NODE_IF_ELSE,
} node_type_t;

/**
 * @brief Checks whether a node type represents a declarator.
 *
 * Declarators introduce a single named entity into a scope: a variable,
 * a constant, or a formal function argument.
 *
 * @param type Node type to check.
 * @return `true` if the type is in the declarator range, otherwise `false`.
 */
static inline bool is_declarator(node_type_t type) {
    return type >= NODE_ARGUMENT && type <= NODE_CONSTANT_DECLARATOR;
}

/**
 * @brief Checks whether a node type represents an expression.
 *
 * Expressions are syntax tree nodes that produce a value when evaluated.
 *
 * @param type Node type to check.
 * @return `true` if the type is in the expression range, otherwise `false`.
 */
static inline bool is_expression(node_type_t type) {
    return type >= NODE_STATEMENT_LIST && type <= NODE_NOT_EQUAL;
}

/**
 * @brief Checks whether a node type represents a statement.
 *
 * Statements are instructions executed sequentially. They may evaluate
 * expressions internally, but are used for control flow, declarations,
 * or side effects rather than for directly producing a value.
 *
 * @param type Node type to check.
 * @return `true` if the type is in the statement range, otherwise `false`.
 */
static inline bool is_statement(node_type_t type) {
    return type >= NODE_STATEMENT_EXPRESSION && type <= NODE_RETURN;
}

/**
 * @brief Checks whether a node type represents a statement list-like expression.
 *
 * Statement list-like nodes contain sequentially executed statements. This
 * includes regular statement lists and function bodies, because apparently one
 * pair of curly braces was not enough semantic ambiguity for this language.
 *
 * @param type Node type to check.
 * @return `true` for NODE_ROOT, NODE_STATEMENT_LIST, NODE_FUNCTION_BODY,
 *         otherwise `false`.
 */
static inline bool is_statement_list(node_type_t type) {
    return type == NODE_ROOT || type == NODE_STATEMENT_LIST || type == NODE_FUNCTION_BODY;
}
