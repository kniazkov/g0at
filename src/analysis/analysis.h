/**
 * @file analysis.h
 * @copyright 2026 Ivan Kniazkov
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
 * @typedef parser_memory_t
 * @brief Forward declaration for the parser memory structure.
 */
typedef struct parser_memory_t parser_memory_t;

/**
 * @typedef options_t
 * @brief Forward declaration for the command line options structure.
 */
typedef struct options_t options_t;

/**
 * @brief Performs static analysis over the abstract syntax tree.
 *
 * Runs the semantic analysis pipeline over the AST. The current pipeline has
 * three main stages:
 *
 * 1. Assign scopes and node metadata.
 *    The analyzer walks the whole AST, assigns parent links, lexical scopes,
 *    and per-scope node identifiers. Function objects are collected into a
 *    queue in depth-first discovery order.
 *
 * 2. Bind declarations and variable usages.
 *    The analyzer processes the queued root/function nodes. Declarators are
 *    registered in scopes, and every variable usage is linked to the declarator
 *    that defines it. If a variable is used before declaration, the analyzer
 *    creates a synthetic declaration request instead of immediately mutating
 *    the AST.
 *
 * 3. Insert synthetic declarations.
 *    Deferred synthetic declarations are inserted into the AST in a separate
 *    pass. Their scope and parent links are then assigned so the final tree
 *    remains structurally valid.
 *
 * After this function completes, the AST has lexical scopes assigned and
 * variable usages linked to declaration nodes, including built-ins and
 * synthetic declarations.
 *
 * @param root_node Root AST node.
 * @param memory Parser memory containing graph and error arenas.
 * @param options Command-line options controlling analysis behavior.
 * @return Linked list of compilation warnings/errors, or NULL if none.
 */
compilation_error_t *analyze(node_t *root_node, parser_memory_t *memory, options_t *options);
