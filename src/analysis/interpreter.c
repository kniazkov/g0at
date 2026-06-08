/**
 * @file interpreter.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the abstract interpretation entry point.
 *
 * This file contains the top-level driver for abstract interpretation.
 *
 * The pass is intentionally small: it creates an empty abstract state, delegates
 * actual node semantics to the AST virtual table, then writes the resulting
 * abstract facts back into declarators. The unpleasant details are left to node
 * implementations, because centralized giant switch statements are how software
 * goes to die wearing a confident expression.
 */

#include "interpreter.h"
#include "abstract_state.h"
#include "lib/arena.h"
#include "graph/node.h"

void interpret(node_t *root_node, parser_memory_t *memory) {
    abstract_state_t *initial = create_abstract_state();
    abstract_state_t *resulting = execute_node(root_node, initial, memory->graph);
    flush_abstract_state(resulting);
    destroy_abstract_state(resulting);
}
