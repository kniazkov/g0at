/**
 * @file interpreter.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Declaration of the abstract interpretation entry point.
 */

#pragma once

typedef struct node_t node_t;

typedef struct parser_memory_t parser_memory_t;

/**
 * @brief Runs abstract interpretation for the syntax tree.
 *
 * After this function completes, abstract values inferred by the interpreter are available directly
 * from declaration nodes.
 */
void interpret(node_t *root_node, parser_memory_t *memory);
