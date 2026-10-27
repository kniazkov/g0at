/** @file switch_warnings.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Duplicate labels proven through closed scalar lattice evaluation.
 */
#include "switch_warnings.h"

#include "comparison.h"
#include "graph/expression.h"
#include "graph/switch.h"
#include "lib/memory/arena.h"
#include "resources/messages.h"

/** @brief Excludes variable lookup, mutable objects, calls, and assignments. */
static bool closed(const node_t *node) {
    node_type_t t = node->vtbl->type;
    bool safe = (t >= NODE_NULL && t <= NODE_CHARACTER) || t == NODE_EXPRESSION_PARENTHESIZED
                || t == NODE_ADDITION || t == NODE_SUBTRACTION
                || (t >= NODE_LOGICAL_NOT && t <= NODE_NOT_EQUAL);
    if (!safe)
        return false;
    for (size_t i = 0; i < get_node_child_count(node); i++)
        if (!closed(get_node_child(node, i)))
            return false;
    return true;
}

static const lattice_element_t *constant(node_t *node, arena_t *arena) {
    if (!closed(node))
        return NULL;
    abstract_state_t *state = create_abstract_state(arena);
    const lattice_element_t *value = calculate_expression((expression_t *)node, state, arena);
    if (state->control_flow != FLOW_NORMAL)
        value = NULL;
    destroy_abstract_state(state);
    return value;
}

void warn_duplicate_cases(node_t *root,
                          arena_t *arena,
                          arena_t *errors,
                          compilation_error_t **list) {
    if (root->vtbl->type == NODE_SWITCH) {
        size_t capacity = get_node_child_count(root), count = 0;
        const lattice_element_t **seen = alloc_from_arena(arena, capacity * sizeof(*seen));
        for (size_t i = 0; i < switch_branch_count(root); i++) {
            const switch_branch_t *branch = switch_branch(root, i);
            for (size_t j = 0; j < branch->count; j++) {
                node_t *label = branch->labels[j];
                const lattice_element_t *value = constant(label, arena);
                if (!value)
                    continue;
                for (size_t k = 0; k < count; k++) {
                    bool equal = value->type == LATTICE_CHAR_CONSTANT
                                         && seen[k]->type == LATTICE_CHAR_CONSTANT
                                     ? ((const char_constant_element_t *)value)->value
                                           == ((const char_constant_element_t *)seen[k])->value
                                     : lattice_truth(lattice_compare(value, seen[k], COMPARE_EQUAL))
                                           == ABSTRACT_TRUE;
                    if (!equal)
                        continue;
                    compilation_error_t *warning =
                        create_error_from_node(errors,
                                               label,
                                               WARNING,
                                               get_messages()->duplicate_case);
                    warning->next = *list;
                    *list = warning;
                    break;
                }
                seen[count++] = value;
            }
        }
    }
    for (size_t i = 0; i < get_node_child_count(root); i++)
        warn_duplicate_cases(get_node_child(root, i), arena, errors, list);
}
