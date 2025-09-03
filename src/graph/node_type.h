/**
 * @file node_type.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Enumeration of node types for the abstract syntax tree (AST).
 *
 * This file defines the @ref node_type_t enumeration, which lists all possible
 * node types used in the abstract syntax tree (AST). Each node type corresponds
 * to a specific language construct.
 */

#pragma once

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
     * @brief Variable declarator node type.
     * 
     * Represents a single variable declarator in a declaration statement.
     * Contains:
     * - Identifier node (required)
     * - Initializer expression node (optional)
     */
    NODE_VARIABLE_DECLARATOR,

    /**
     * @brief Constant declaration statement node type.
     * 
     * This node represents a constant declaration statement (e.g., "const pi = 3.14").
     * Contains child nodes of type NODE_CONSTANT_DECLARATOR for each declared constant.
     * The node itself serves as a container and doesn't store specific values.
     */
    NODE_CONSTANT_DECLARATION,

    /**
     * @brief Constant declarator node type.
     * 
     * Represents a single constant declarator in a declaration statement.
     * Contains:
     * - Identifier node (required)
     * - Initializer expression node (required)
     */
    NODE_CONSTANT_DECLARATOR,

    /**
     * @brief Return statement node type.
     * 
     * Represents a `return` statement in the syntax tree. May optionally include
     * a value expression to be returned from the function.
     * Contains:
     * - Return value expression node (optional)
     */
    NODE_RETURN,
} node_type_t;
