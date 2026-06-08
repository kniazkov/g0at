/**
 * @file declarations.h
 * @copyright 2026 Ivan Kniazkov
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
 * @typedef lattice_element_t
 * @brief Forward declaration for an abstract-interpretation lattice element.
 */
typedef struct lattice_element_t lattice_element_t;

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

    /**
     * @brief Final abstract value inferred for this declaration.
     *
     * This field is filled by the abstract interpretation pass after name
     * resolution, scope analysis, and declaration binding have already been
     * completed.
     *
     * Unlike the abstract state used during interpretation, this field does not
     * describe a temporary program point. It stores the final summarized fact
     * known about the declared entity after all relevant control-flow paths have
     * been processed and joined. In human terms, this is where the compiler stops
     * thinking and writes down the least embarrassing answer it managed to prove.
     *
     * For example:
     *
     *     var x = 0;
     *
     * may initially produce the abstract value:
     *
     *     integer constant 0
     *
     * while:
     *
     *     if (condition) {
     *         x = 1;
     *     }
     *
     * may later widen the final declaration fact to:
     *
     *     integer interval [0..1]
     *
     * The value is stored directly in the declarator so later compiler stages can
     * use it without replaying the analysis. Code generation may use this fact for
     * constant folding, narrowing runtime checks, selecting more specific bytecode,
     * or eventually emitting more efficient native C code. Naturally, this also
     * gives the compiler another opportunity to feel superior to the source code.
     *
     * The visualization layer may also print this field in the AST graph, making
     * abstract interpretation results visible next to the declaration that owns
     * them.
     */
    const lattice_element_t *abstract_value;
} declarator_t;

/**
 * @struct variable_declaration_pair_t
 * @brief Pair of a variable declaration node and its single declarator.
 *
 * Used when creating synthetic declarations during static analysis. The
 * declaration node is the statement inserted into the AST, while the declarator
 * node is returned separately so other nodes can reference it directly.
 */
typedef struct {
    /**
     * @brief Variable declaration statement node.
     */
    node_t *declaration;

    /**
     * @brief Single variable declarator contained by the declaration.
     */
    declarator_t *declarator;
} variable_declaration_pair_t;

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

/**
 * @brief Creates a synthetic declaration of one variable without an initializer.
 *
 * Creates a variable declaration node containing exactly one variable declarator
 * with the specified name and no initial value.
 *
 * @param arena Arena allocator for memory management.
 * @param name Name of the synthetic variable.
 * @return Pair containing the declaration node and its single declarator node.
 */
variable_declaration_pair_t create_synthetic_variable_declaration_node(arena_t *arena,
        string_view_t name);

/**
 * @brief Gets the built-in declarator placeholder.
 *
 * Returns a singleton fake declarator used for built-in names that are provided
 * by the runtime or standard environment and therefore have no explicit
 * declaration in user source code.
 *
 * The declarator has an invalid name and must not be emitted as a real source
 * declaration. It exists only so resolved built-ins can still point to a
 * non-NULL declarator during static analysis and graph construction.
 *
 * @return Pointer to the built-in declarator singleton.
 */
const declarator_t *get_builtin_declarator();
