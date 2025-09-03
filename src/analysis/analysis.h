/**
 * @file analysis.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Declarations for static code analysis functions.
 *
 * This header defines the public interface for the static analysis subsystem.
 * Static analysis operates on the abstract syntax tree (AST) to:
 * - Assign unique identifiers to nodes within scopes
 * - Link nodes to their lexical scopes
 * - Populate symbol tables with bindings
 * - Perform semantic checks such as undeclared identifiers, duplicate definitions,
 *   unreachable code, and type consistency
 *
 * The functions declared here form the foundation for the compiler’s semantic phase.
 */

#pragma once

/**
 * @brief Forward declaration of compilation error structure.
 */
typedef struct compilation_error_t compilation_error_t;

/**
 * @typedef node_t
 * @brief Forward declaration for the node structure.
 */
typedef struct node_t node_t;

/**
 * @typedef arena_t
 * @brief Forward declaration for the memory arena structure.
 */
typedef struct arena_t arena_t;

/**
 * @brief Performs static analysis on the given syntax tree.
 *
 * This is the entry point for the semantic/static analysis phase of the
 * compiler. The function traverses the syntax tree, invoking a sequence of
 * analysis stages such as:
 * - Assigning unique identifiers to scopes and nodes
 * - Building scope chains and symbol bindings
 * - Resolving identifiers and references
 * - Performing semantic checks (e.g., undeclared identifiers, duplicate declarations,
 *   unreachable code)
 * - Etc
 *
 * @param root_node Pointer to the root AST node.
 * @param arena Memory arena used for allocating auxiliary structures, such as scopes, bindings,
 *  and error reports.
 * @return Pointer to a linked list of compilation errors discovered during analysis, or NULL
 *  if the analysis completed successfully without errors.
 */
compilation_error_t *analyze(node_t *root_node, arena_t *arena);
