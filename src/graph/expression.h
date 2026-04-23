/**
 * @file expression.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Definition of the expression structure.
 * 
 * This file defines the `expression_t` structure, which represents an expression node in
 * the syntax tree of the language.
 */

#pragma once

#include "node.h"
#include "data_type.h"

/**
 * @brief Forward declaration of declarator structure (needed for variable & constant declaration).
 */
typedef struct base_declarator_t base_declarator_t;

/**
 * @struct expression_t
 * @brief The structure representing an expression node.
 */
struct expression_t {
    /**
     * @brief Base node structure, providing common attributes for all nodes.
     */
    node_t base;

    /**
     * @brief The semantic data type of this expression (optional).
     *
     * Points to the @ref data_type_t describing the type of the expression.
     * May be NULL for untyped/invalid expressions during early parsing or
     * when the type is inferred/unknown at the current stage.
     */
    const data_type_t *data_type;
};

/**
 * @brief Gets the primary string data associated with an expression.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param expr A pointer to the expression.
 * @return A `string_value_t` containing the expression data or empty value if none.
 */
static inline string_value_t get_expression_data(const expression_t *expr) {
    return get_node_data(&expr->base);
}

/**
 * @brief Gets the number of properties exposed by an expression.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param expr A pointer to the expression.
 * @return Number of properties exposed by the expression.
 */
static inline size_t get_expression_property_count(const expression_t *expr) {
    return get_node_property_count(&expr->base);
}

/**
 * @brief Retrieves a property of an expression by index.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param expr A pointer to the expression.
 * @param index Zero-based property index.
 * @param out_key Output pointer to receive the property key.
 * @param out_value Output pointer to receive the property value.
 */
static inline void get_expression_property(const expression_t *expr, size_t index,
        string_view_t *out_key, string_value_t *out_value) {
    get_node_property(&expr->base, index, out_key, out_value);
}

/**
 * @brief Gets the number of direct child nodes of an expression.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param expr A pointer to the expression.
 * @return Number of child nodes.
 */
static inline size_t get_expression_child_count(const expression_t *expr) {
    return get_node_child_count(&expr->base);
}

/**
 * @brief Gets a child node of an expression by index.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param expr A pointer to the expression.
 * @param index Zero-based child index.
 * @return Pointer to the child node or NULL if index is out of range.
 */
static inline node_t *get_expression_child(const expression_t *expr, size_t index) {
    return get_node_child(&expr->base, index);
}

/**
 * @brief Gets the tag/label for a child node of an expression.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param expr A pointer to the expression.
 * @param index Zero-based child index.
 * @return Wide character string with the child tag or NULL if not applicable.
 */
static inline const wchar_t *get_expression_child_tag(const expression_t *expr, size_t index) {
    return get_node_child_tag(&expr->base, index);
}

/**
 * @brief Generates a single-line Goat source code representation from an expression.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param expr A pointer to the expression.
 * @return A `string_value_t` containing the generated Goat code.
 */
static inline string_value_t generate_goat_code_from_expression(const expression_t *expr) {
    return generate_goat_code_from_node(&expr->base);
}

/**
 * @brief Generates indented Goat source code from an expression.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param expr A pointer to the expression.
 * @param builder A pointer to the source builder.
 * @param indent The number of tabs used for indentation.
 */
static inline void generate_indented_goat_code_from_expression(const expression_t *expr,
        source_builder_t *builder, size_t indent) {
    generate_indented_goat_code_from_node(&expr->base, builder, indent);
}

/**
 * @brief Checks whether C code can be generated from an expression.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param expr A pointer to the expression.
 * @return `true` if C code generation is supported, `false` otherwise.
 */
static inline bool can_generate_c_code_from_expression(const expression_t *expr) {
    return can_generate_c_code_from_node(&expr->base);
}

/**
 * @brief Generates a single-line C source code representation from an expression.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param expr A pointer to the expression.
 * @return A `string_value_t` containing the generated C code or NULL string if
 *  conversion is not possible.
 */
static inline string_value_t generate_c_code_from_expression(const expression_t *expr) {
    return generate_c_code_from_node(&expr->base);
}

/**
 * @brief Generates indented C source code from an expression.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param expr A pointer to the expression.
 * @param builder A pointer to the source builder.
 * @param indent The number of tabs used for indentation.
 */
static inline void generate_indented_c_code_from_expression(const expression_t *expr,
        source_builder_t *builder, size_t indent) {
    generate_indented_c_code_from_node(&expr->base, builder, indent);
}

/**
 * @brief Generates bytecode from an expression.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param expr A pointer to the expression.
 * @param code A pointer to the code builder used for instruction emission.
 * @param data A pointer to the data builder used for static data management.
 * @return The instruction index of the first emitted instruction.
 */
static inline instr_index_t generate_bytecode_from_expression(expression_t *expr,
        code_builder_t *code, data_builder_t *data) {
    return generate_bytecode_from_node(&expr->base, code, data);
}

/**
 * @brief Generates bytecode for storing a value into an expression.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param expr A pointer to the target expression.
 * @param code A pointer to the code builder.
 * @param data A pointer to the data builder.
 * @return The instruction index of the first emitted instruction.
 */
static inline instr_index_t generate_bytecode_assign_from_expression(const expression_t *expr,
        code_builder_t *code, data_builder_t *data) {
    return generate_bytecode_assign_from_node(&expr->base, code, data);
}

/**
 * @brief Generates deferred bytecode from an expression.
 *
 * This helper forwards the request to the underlying base node helper.
 *
 * @param expr A pointer to the expression.
 * @param code A pointer to the code builder.
 * @param data A pointer to the data builder.
 * @return `true` if deferred bytecode was successfully generated in this pass;
 *  `false` otherwise.
 */
static inline bool generate_deferred_bytecode_from_expression(const expression_t *expr,
        code_builder_t *code, data_builder_t *data) {
    return generate_deferred_bytecode_from_node(&expr->base, code, data);
}

/**
 * @brief Creates a new static string expression node.
 * 
 * This function initializes a static string expression node with the given string value
 * and its length. The string and the node are allocated in the specified memory arena.
 * 
 * @param arena A pointer to the memory arena for allocation.
 * @param data The string value for the static string expression.
 * @param length The length of the string (excluding null terminator).
 * @return A pointer to the newly created static string node, represented as a `node_t`.
 */
node_t *create_static_string_node(arena_t *arena, const wchar_t *data, size_t length);

/**
 * @brief Creates a new integer literal node.
 * 
 * Allocates and initializes a new integer literal node in the specified arena.
 * The node will contain the provided 64-bit integer value.
 * 
 * @param arena Memory arena for node allocation.
 * @param value The integer value to store in the node.
 * @return A pointer to the newly created integer node, represented as a `node_t`.
 */
node_t *create_integer_node(arena_t *arena, int64_t value);

/**
 * @brief Creates a real number literal expression node.
 * 
 * Allocates and initializes a `real_t` node in the given arena.
 * 
 * @param arena Memory arena for allocation.
 * @param value The 64-bit floating-point value to store.
 * @return A pointer to the created AST node.
 */
node_t *create_real_number_node(arena_t *arena, double value);

/**
 * @brief Creates a new variable expression node.
 * 
 * This function allocates and initializes a new variable expression node in the syntax tree.
 * The node represents a variable identified by its name.
 * 
 * @param arena The memory arena for allocating the variable node.
 * @param name String representing the variable's name.
 * @return A pointer to the newly created variable expression node.
 */
expression_t *create_variable_node(arena_t *arena, string_view_t name);

/**
 * @brief Creates a declarator from an existing variable expression.
 * 
 * Constructs a new declarator structure to represent the declaration of
 * an existing variable. The resulting declarator can be used in declaration
 * nodes (like variable_declaration_t) to formally declare the variable.
 * 
 * @param expr Pointer to the variable expression to convert.
 * @return Pointer to newly allocated base_declarator_t with name copied from the variable and no
 *  initializer.
 * @warning The returned declarator is heap-allocated and must be freed by the caller
 *  when no longer needed.
 * @note The created declarator will have no initializer (NULL), which is valid for
 *  variable declarations but invalid for constant declarations.
 */
base_declarator_t *create_declarator_from_variable(const node_t *expr);

/**
 * @brief Creates a function call expression node with empty arguments.
 * 
 * This function initializes a function call node without arguments, which is useful when:
 * - The function call syntax is recognized early in parsing (high precedence)
 * - The arguments contain complex expressions that need to be parsed later
 * - The function object is known but arguments require additional processing
 * 
 * @param arena Memory arena for allocation.
 * @param func_object The callable expression (function object).
 * @return Pointer to the created function call node.
 * 
 * @note The arguments must be set later using set_function_call_arguments()
 * @see set_function_call_arguments
 */
node_t *create_function_call_node_without_args(arena_t *arena, expression_t *func_object);

/**
 * @brief Sets arguments for a previously created function call node.
 * 
 * This function completes the initialization of a function call node by setting its arguments.
 * 
 * @param node Function call node created with create_function_call_node_without_args().
 * @param arena Memory arena for argument array allocation.
 * @param args Array of argument expressions.abort
 * @param args_count Number of arguments.
 * 
 * @pre The node must be of function call type (`NODE_FUNCTION_CALL`)
 * @pre The node must not have arguments already set
 * 
 * @note This operation cannot be undone - arguments can only be set once.
 */
void set_function_call_arguments(node_t *node, arena_t *arena, 
        expression_t **args, size_t args_count);

/**
 * @brief Creates an empty statement_list node.
 * @param arena Memory arena to allocate from.
 * @return Pointer to the newly created node.
 */
node_t *create_statement_list_node(arena_t *arena);

/**
 * @brief Initializes a statement_list node with a provided array of statements.
 *
 * Copies the array of statement pointers into arena-managed memory and sets the count.
 *
 * @param node Target node (must be of type `NODE_STATEMENT_LIST`).
 * @param arena Memory arena used for internal allocations.
 * @param stmt_list Source array of statement pointers.
 * @param stmt_count Number of statements.
 */
void fill_statement_list_node(node_t *node, arena_t *arena, statement_t **stmt_list,
    size_t stmt_count);

/**
 * @brief Creates a function object node in the AST.
 * 
 * Allocates and initializes a function object node using the given argument list.
 * The function parameters are copied into the arena. The function body is not set here
 * and must be filled later using `fill_function_body`.
 * 
 * @param arena The arena allocator for memory management.
 * @param arg_list The list of function parameter names.
 * @param arg_count The number of parameters.
 * @return A pointer to the newly created function object node (as `node_t`).
 */
node_t *create_function_object_node(arena_t *arena, string_view_t *arg_list, size_t arg_count);

/**
 * @brief Fills in the body of a function object node.
 * 
 * Copies the list of statements forming the function body into the function object.
 * This must be called after creating the node via `create_function_object_node`.
 * 
 * @param node A pointer to a node previously created as a function object.
 * @param arena The arena allocator for memory management.
 * @param stmt_list An array of pointers to statement nodes.
 * @param stmt_count The number of statements in the function body.
 */
void fill_function_body(node_t *node, arena_t *arena, statement_t **stmt_list, size_t stmt_count);

/**
 * @brief Creates a new parenthesized expression node with no inner expression.
 *
 * @param arena The memory arena to allocate from.
 * @return A pointer to the created expression node.
 */
node_t *create_parenthesized_expression_node(arena_t *arena);

/**
 * @brief Fills the inner expression of a parenthesized expression node.
 *
 * This function assigns the wrapped expression after node creation.
 *
 * @param node A pointer to the node representing a parenthesized expression.
 * @param inner A pointer to the inner expression to wrap.
 */
void fill_parenthesized_expression(node_t *node, expression_t *inner);

/**
 * @brief Creates a new null literal expression node.
 *
 * @param arena Memory arena used for node allocation.
 * @return Pointer to the created null literal node.
 */
node_t *create_null_node(arena_t *arena);

/**
 * @brief Creates a new boolean `true` literal expression node.
 *
 * @param arena Memory arena used for node allocation.
 * @return Pointer to the created `true` literal node.
 */
node_t *create_true_node(arena_t *arena);

/**
 * @brief Creates a new boolean `false` literal expression node.
 *
 * @param arena Memory arena used for node allocation.
 * @return Pointer to the created `false` literal node.
 */
node_t *create_false_node(arena_t *arena);
