/**
 * @file declarations.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Definitions for declaration-related structures and functions.
 *
 * This file defines auxiliary structures and factory functions used for
 * variable and constant declarations in the abstract syntax tree (AST).
 * It contains both lightweight declarator specifications used during parsing
 * and base AST structures for declarator nodes.
 */

#pragma once

#include "node.h"

/**
 * @brief Forward declaration of expression structure.
 */
typedef struct expression_t expression_t;

/**
 * @struct declarator_spec_t
 * @brief Represents a single declaration specification.
 *
 * This structure encapsulates the common properties of both variable and constant
 * declarations before a full AST node is created, including the identifier name
 * and optional initializer expression.
 */
typedef struct declarator_spec_t {
    /**
     * @brief The name of the declared identifier.
     */
    string_view_t name;

    /**
     * @brief The initializer expression for the declaration.
     *
     * - For variables: Optional (may be `NULL`)
     * - For constants: Required (must be non-`NULL`)
     */
    expression_t *initial;
} declarator_spec_t;

/**
 * @struct declarator_t
 * @brief Base structure for declaration nodes in the abstract syntax tree.
 *
 * This structure serves as a common base for concrete declarator node types,
 * such as variable declarators and constant declarators. It stores the name of
 * the declared entity and can later be extended with additional fields filled
 * during static analysis.
 */
typedef struct declarator_t {
    /**
     * @brief Base node structure.
     *
     * Provides common node functionality and allows this structure to be treated
     * as a regular AST node.
     */
    node_t base;

    /**
     * @brief The name of the declared entity.
     */
    string_view_t name;
} declarator_t;

/**
 * @brief Creates a new variable declaration AST node.
 *
 * Constructs a complete variable declaration statement node containing one or more
 * variable declarators. The node owns the declarator list and its contents.
 *
 * @param arena Arena allocator for memory management.
 * @param decl_list Array of declarator specifications.
 * @param decl_count Number of declarators (must be > 0).
 * @return Pointer to the newly created variable declaration node.
 */
node_t *create_variable_declaration_node(arena_t *arena, declarator_spec_t **decl_list,
        size_t decl_count);

/**
 * @brief Creates a new constant declaration AST node.
 *
 * Constructs a complete constant declaration statement node containing one or more
 * constant declarators. The node owns the declarator list and its contents.
 *
 * @param arena Arena allocator for memory management.
 * @param decl_list Array of declarator specifications.
 * @param decl_count Number of declarators (must be > 0).
 * @return Pointer to the newly created constant declaration node.
 *
 * @warning All declarators must have non-`NULL` initializers.
 */
node_t *create_constant_declaration_node(arena_t *arena, declarator_spec_t **decl_list,
        size_t decl_count);