/**
 * @file interpreter.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the abstract interpretation entry point.
 */

#include "interpreter.h"
#include "abstract_state.h"
#include "lib/arena.h"
#include "graph/node.h"

void interpret(node_t *root_node, parser_memory_t *memory,
        analysis_collector_t *collector) {
    abstract_state_t *initial = create_abstract_state(memory->graph);
    initial->collector = collector;
    abstract_state_t *resulting = execute_node(root_node, initial, memory->graph);
    flush_abstract_state(resulting);
    destroy_abstract_state(resulting);
}
