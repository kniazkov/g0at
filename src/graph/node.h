/**
 * @file node.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Definitions for the basic node structure of the abstract syntax tree (AST).
 * 
 * This file contains the definition of the basic node structure for the abstract syntax tree (AST).
 * The AST is a tree representation of the syntactic structure of source code, where each node
 * represents a language construct (e.g., expression, statement, or declaration).
 */

#pragma once

#include "lib/value.h"
#include "common/types.h"

/**
 * @typedef node_t
 * @brief Forward declaration for the node structure.
 */
typedef struct node_t node_t;

/**
 * @typedef statement_t
 * @brief Forward declaration for the statement structure.
 */
typedef struct statement_t statement_t;

/**
 * @typedef expression_t
 * @brief Forward declaration for the expression structure.
 */
typedef struct expression_t expression_t;

/**
 * @typedef source_builder_t
 * @brief Forward declaration for the source code builder structure.
 */
typedef struct source_builder_t source_builder_t;

/**
 * @typedef code_builder_t
 * @brief Forward declaration for the code builder structure.
 */
typedef struct code_builder_t code_builder_t;

/**
 * @typedef data_builder_t
 * @brief Forward declaration for the data builder structure.
 */
typedef struct data_builder_t data_builder_t;

/**
 * @typedef arena_t
 * @brief Forward declaration for the arena memory allocator.
 */
typedef struct arena_t arena_t;

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
     * @brief Scope node type.
     * 
     * This node represents a set of instructions executing in some scope.
     */
    NODE_SCOPE,

    /**
     * @brief Null literal node type.
     * 
     * This node type represents a null literal in the source code, indicating the absence
     * of a value. The node contains no additional data as null is a singleton value.
     */
    NODE_NULL,

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
     * @brief Variable node type.
     * 
     * This node type represents a variable expression in the source code.
     * It stores the name of the variable, and can be used to represent variables
     * or other named entities (e.g., constants).
     */
    NODE_VARIABLE,

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
     * This node type represents a binary addition operation in the source code.
     * It stores the left and right operands of the addition.
     */
    NODE_ADDITION,

    /**
     * @brief Subtraction operation node type.
     * 
     * This node type represents a binary subtraction operation in the source code.
     * It stores the left and right operands of the subtraction.
     */
    NODE_SUBTRACTION,

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
    NODE_CONSTANT_DECLARATOR
} node_type_t;

/**
 * @struct node_vtbl_t
 * @brief The virtual table structure for nodes in the syntax tree.
 * 
 * This structure contains pointers to functions that implement the behavior of different types
 * of nodes in the syntax tree. The virtual table allows polymorphism, enabling different node
 * types to have their own implementations for specific operations.
 */
typedef struct {
    /**
     * @brief The type of the node.
     * 
     * Identifies the specific type of the syntax tree node. Used for distinguishing between
     * different node implementations.
     */
    node_type_t type;

    /**
     * @brief Human-readable type name of the node.
     * 
     * A wide character string representing the type name of the node
     * (e.g., "function declaration", "addition", "variable").
     */
    const wchar_t* type_name;

    /**
     * @brief Flag indicating whether the node represents an assignable expression (lvalue).
     * 
     * When true, indicates that:
     * - The node can appear on the left-hand side of an assignment
     * - The `generate_bytecode_assign` method must be implemented
     * - The node represents a valid storage location (variable, array access, etc.)
     */
    bool is_assignable_expression;

    /**
     * @brief Gets the string representation of the node's data.
     * 
     * Returns the primary data associated with the node in string form.
     * For example:
     * - For an identifier node: the identifier name
     * - For a literal node: the literal value
     * - For operators: the operator symbol
     * 
     * @param node A pointer to the node.
     * @return A `string_value_t` containing the node's data or empty value if none.
     */
    string_value_t (*get_data)(const node_t *node);

    /**
     * @brief Gets the number of child nodes.
     * 
     * Returns the count of direct child nodes for this syntax tree node.
     * 
     * @param node A pointer to the node.
     * @return Number of child nodes (0 for leaf nodes).
     */
    size_t (*get_child_count)(const node_t *node);

    /**
     * @brief Gets a child node by index.
     * 
     * Retrieves a specific child node from the syntax tree.
     * 
     * @param node A pointer to the parent node.
     * @param index Zero-based index of the child node.
     * @return Pointer to the child node or NULL if index is out of range.
     */
    const node_t* (*get_child)(const node_t *node, size_t index);

    /**
     * @brief Gets the tag/label for a child node.
     * 
     * Returns a descriptive wide character string that labels the relationship
     * between the parent node and the specified child node (e.g., "left", "right", 
     * "condition", "body", "parameters").
     * 
     * @param node A pointer to the parent node.
     * @param index Zero-based index of the child node.
     * @return Wide character string (const wchar_t*) with the child's tag or NULL
     * if not applicable.
     */
    const wchar_t* (*get_child_tag)(const node_t *node, size_t index);

    /**
     * @brief Generates a single-line Goat source code representation of the node.
     * 
     * This function returns a compact Goat source code representation of the node
     * without indentation.
     * While the generated code is functionally equivalent to the original source from which 
     * the syntax tree was created, it may not be a literal reproduction due to optimizations 
     * or transformations applied during parsing and processing.
     * 
     * @param node A pointer to the node.
     * @return A `string_value_t` containing the generated Goat code.
     */
    string_value_t (*generate_goat_code)(const node_t *node);

    /**
     * @brief Generates indented Goat source code for the node, if applicable.
     * 
     * This function produces formatted Goat source code with proper indentation.
     * It applies only to node types that logically support indentation (e.g., blocks, functions).
     * The generated code is functionally equivalent to the original, but may differ in formatting 
     * and structure due to optimizations or transformations.
     * 
     * @param node A pointer to the node.
     * @param builder A pointer to the `source_builder_t` to store the generated output.
     * @param indent The number of tabs used for indentation.
     */
    void (*generate_indented_goat_code)(const node_t *node, source_builder_t *builder,
            size_t indent);

    /**
     * @brief Checks if C code generation is possible for this node.
     * 
     * This function determines whether the node has a valid representation in C.
     * If `true`, `to_c()` and `to_c_indent()` will return meaningful output.
     * 
     * @param node A pointer to the node.
     * @return `true` if C generation is supported, `false` otherwise.
     */
    bool (*can_generate_c_code)(const node_t *node);

    /**
     * @brief Generates a single-line C source code representation of the node, if possible.
     * 
     * This function attempts to produce an equivalent C representation of the node without
     * indentation. If the node cannot be represented in C, it returns NULL string.
     * 
     * @param node A pointer to the node.
     * @return A `string_value_t` containing the generated C code or NULL string if conversion
     *  is not possible.
     */
    string_value_t (*generate_c_code)(const node_t *node);

    /**
     * @brief Generates indented C source code for the node, if applicable.
     * 
     * This function attempts to generate formatted C code with proper indentation.
     * It applies only to nodes that have a valid C equivalent and require indentation
     * (e.g., function bodies, loops).
     * 
     * @param node A pointer to the node.
     * @param builder A pointer to the `source_builder_t` to store the generated output.
     * @param indent The number of tabs used for indentation.
     */
    void (*generate_indented_c_code)(const node_t *node, source_builder_t *builder, size_t indent);

    /**
     * @brief Generates bytecode for the given node.
     * 
     * This function generates the bytecode instructions for a node in the syntax tree. It takes
     * two additional arguments, `code_builder_t` and `data_builder_t`, which are used to manage
     * the bytecode instructions and static data respectively. The node's specific implementation
     * will determine how it contributes to the final bytecode output.
     * 
     * @param node A pointer to the node for which bytecode is being generated.
     * @param code A pointer to the `code_builder_t` used to add bytecode instructions.
     * @param data A pointer to the `data_builder_t` used to manage static data.
     * @return The instruction index of the first emitted instruction.
     */
    instr_index_t (*generate_bytecode)(node_t *node, code_builder_t *code, data_builder_t *data);

    /**
     * @brief Generates bytecode for storing a value into this expression.
     * 
     * This method generates the bytecode needed to store a value (expected to be
     * on top of the stack) into the location represented by this expression.
     * Should only be implemented for assignable expressions (lvalues).
     * 
     * @param node A pointer to the target node.
     * @param code A pointer to the code builder for bytecode generation.
     * @param data A pointer to the data builder for static data management.
     * @return The instruction index of the first emitted instruction.
     */
    instr_index_t (*generate_bytecode_assign)(const node_t *node, code_builder_t *code,
        data_builder_t *data);

    /**
     * @brief Generates deferred bytecode for the given node.
     * 
     * This method generates "deferred" bytecode for a node — code that is not executed immediately
     * during the main flow, but instead triggered later, such as function bodies
     * or lazy expressions.
     * It returns the index of the first instruction of the generated code, which can later be used
     * to reference or jump to this code block.
     * 
     * @param node A pointer to the node for which deferred bytecode is being generated.
     * @param code A pointer to the code builder for bytecode generation.
     * @param data A pointer to the data builder for static data management.
     * @return The instruction index of the start of the generated deferred bytecode.
     */
    instr_index_t (*generate_bytecode_deferred)(const node_t *node, code_builder_t *code,
        data_builder_t *data);
} node_vtbl_t;

/**
 * @struct node_t
 * @brief The base structure for nodes in the syntax tree.
 * 
 * This structure represents a base node in the syntax tree of the Goat language. All nodes,
 * regardless of their specific type, share this common structure which contains a reference to
 * their virtual table. The virtual table allows polymorphic behavior for nodes of different types,
 * enabling specific behavior for each node type in the tree.
 * 
 * Nodes in the syntax tree represent various constructs in the parsed source code, and can
 * be extended with additional fields for specific node types as necessary.
 */
struct node_t {
    /**
     * @brief Pointer to the node's virtual table.
     * 
     * This field holds a pointer to the virtual table that contains function pointers specific
     * to the type of node. It allows polymorphic behavior and provides type-specific
     * implementations for operations on different node types in the tree.
     * 
     * @note The virtual table is typically used to safely cast `node_t` to a more 
     *  specific node type.
     */
    node_vtbl_t *vtbl;
};

/**
 * @brief Creates a new root node.
 * 
 * This function allocates memory for a new root node, copies the provided statement list into
 * the node, and returns a pointer to the newly created node.
 * 
 * @param arena A pointer to the memory arena for memory allocation.
 * @param stmt_list A list of statements to be included in the root node.
 * @param stmt_count The number of statements in the list.
 * @return A pointer to the newly created root node.
 */
node_t *create_root_node(arena_t *arena, statement_t **stmt_list, size_t stmt_count);
