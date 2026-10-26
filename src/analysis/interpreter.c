/**
 * @file interpreter.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the abstract interpretation entry point.
 */

#include "interpreter.h"

#include "abstract_state.h"
#include "graph/node.h"
#include "lib/arena.h"

void interpret(node_t *root_node, parser_memory_t *memory, analysis_collector_t *collector) {
    abstract_state_t *initial = create_abstract_state(memory->graph);
    initial->collector = collector;
    initial->concurrent_bindings = requires_concurrency_safe_optimization(root_node);
    abstract_state_t *resulting = execute_node(root_node, initial, memory->graph);
    flush_abstract_state(resulting);
    destroy_abstract_state(resulting);
}

void interpret_module(node_t *root_node, parser_memory_t *memory, analysis_collector_t *collector) {
    abstract_state_t *initial = create_abstract_state(memory->graph);
    initial->collector = collector;
    initial->concurrent_bindings = requires_concurrency_safe_optimization(root_node);
    initial->module_constants = true;
    abstract_state_t *resulting = execute_node(root_node, initial, memory->graph);
    flush_abstract_state(resulting);
    destroy_abstract_state(resulting);
}
