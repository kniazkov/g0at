/**
 * @file interpreter.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Declaration of the abstract interpretation entry point.
 *
 * This module defines the public interface of the abstract interpreter pass.
 *
 * The abstract interpreter walks the already parsed and semantically prepared
 * syntax tree, executing node-specific abstract semantics instead of concrete
 * runtime instructions. It does not run the program. It computes facts about
 * declarations and expressions using lattice elements, because apparently even
 * values need to be domesticated before code generation is allowed near them.
 *
 * The main result of this pass is written back into the AST: final abstract
 * values are flushed into declarators, so later stages such as graph generation,
 * bytecode generation, optimization, or future C/native code generation can use
 * those facts without rerunning the analysis.
 */

#pragma once

/**
 * @typedef node_t
 * @brief Forward declaration for the base AST node structure.
 */
typedef struct node_t node_t;

/**
 * @typedef parser_memory_t
 * @brief Forward declaration for parser-owned memory and analysis context.
 */
typedef struct parser_memory_t parser_memory_t;

/**
 * @brief Runs abstract interpretation for the syntax tree.
 *
 * Creates the initial empty abstract state, executes abstract semantics starting
 * from the root node, flushes the resulting facts into corresponding AST
 * declarators, and destroys the temporary state container.
 *
 * After this function completes, abstract values inferred by the interpreter are
 * available directly from declaration nodes.
 *
 * @param root_node Root node of the syntax tree to interpret.
 * @param memory Parser memory and analysis context.
 */
void interpret(node_t *root_node, parser_memory_t *memory);
