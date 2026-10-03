/**
 * @file declarations.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definitions for declaration-related structures and functions.
 */

#pragma once

#include "node.h"

typedef struct expression_t expression_t;
typedef struct lattice_element_t lattice_element_t;

/**
 * @brief A single declaration specification.
 *
 * Encapsulates the common properties of both variable and constant declarations before a full AST
 * node is created, including the identifier name and optional initializer expression.
 */
typedef struct declarator_spec_t {
    /** @brief The name of the declared identifier. */
    string_view_t name;

    /**
     * @brief The initializer expression for the declaration.
     *
     * - For variables: Optional (may be `NULL`) - For constants: Required (must be non-`NULL`)
     */
    expression_t *initial;
} declarator_spec_t;

/** @brief Base structure for declaration nodes in the abstract syntax tree. */
typedef struct declarator_t {
    /** @brief Base node structure. */
    node_t base;

    /** @brief The name of the declared entity. */
    string_view_t name;

    /** @brief Joined abstract value assigned by analysis; NULL until computed. */
    const lattice_element_t *abstract_value;
} declarator_t;

/** @brief Pair of a variable declaration node and its single declarator. */
typedef struct {
    /** @brief Variable declaration statement node. */
    node_t *declaration;

    /** @brief Single variable declarator contained by the declaration. */
    declarator_t *declarator;
} variable_declaration_pair_t;

/**
 * @brief Creates a new variable declaration AST node.
 *
 * The node owns the declarator list and its contents.
 * `decl_count`: Number of declarators (must be > 0).
 */
node_t *
create_variable_declaration_node(arena_t *arena, declarator_spec_t **decl_list, size_t decl_count);

/**
 * @brief Creates a new constant declaration AST node.
 *
 * The node owns the declarator list and its contents.
 * `decl_count`: Number of declarators (must be > 0).
 * @warning All declarators must have non-`NULL` initializers.
 */
node_t *
create_constant_declaration_node(arena_t *arena, declarator_spec_t **decl_list, size_t decl_count);

/** @brief Creates a synthetic declaration of one variable without an initializer. */
variable_declaration_pair_t create_synthetic_variable_declaration_node(arena_t *arena,
                                                                       string_view_t name);

/**
 * @brief Gets the built-in declarator placeholder.
 *
 * The declarator has an invalid name and must not be emitted as a real source declaration. It
 * exists only so resolved built-ins can still point to a non-NULL declarator during static analysis
 * and graph construction.
 */
const declarator_t *get_builtin_declarator();

/**
 * @brief Gets the number of visualization/debug properties exposed by a declarator.
 *
 * Declarators expose the final abstract value inferred for the declared entity after abstract
 * interpretation has flushed its results into the AST.
 */
size_t get_property_count_of_declarator(const node_t *node);

/**
 * @brief Retrieves a property of a declarator by index.
 * `index`: Zero-based property index.
 * @return Property key as a constant wide string, or NULL if unavailable.
 */
const wchar_t *
get_property_of_declarator(const node_t *node, size_t index, node_display_value_t *out_value);
