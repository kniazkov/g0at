/** @file parsing_inheritance.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Prototype construction is distinct from ordinary binary operations.
 */
#include "graph/inheritance.h"
#include "parser.h"
#include "resources/messages.h"

compilation_error_t *
parsing_inheritance(token_t *op, parser_memory_t *memory, token_groups_t *groups) {
    token_t *left = op->left, *right = op->right;
    if (!left || left->type != TOKEN_EXPRESSION || !right || right->type != TOKEN_EXPRESSION
        || right->node->vtbl->type != NODE_STATEMENT_LIST)
        return create_error_from_token(memory->errors,
                                       op,
                                       CRITICAL,
                                       get_messages()->expected_inheritance_constructor);
    node_t *node = create_inheritance_node(memory->graph,
                                           (expression_t *)left->node,
                                           (statement_list_t *)right->node);
    collapse_tokens_to_token(memory, left, right, TOKEN_EXPRESSION, node);
    return NULL;
}
