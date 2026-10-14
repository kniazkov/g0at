/**
 * @file parsing_member_access.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines the reduction rule for member access (`.` operator).
 */

#include "graph/member_access.h"
#include "graph/variable.h"
#include "lib/arena.h"
#include "parser.h"

#include <assert.h>

/**
 * @brief Rule for handling member access: an expression followed by a `.` and an identifier.
 *
 * The right operand must be a plain identifier (the property name). The left operand may already be
 * an expression (e.g. a parenthesized expression or a previously reduced chain), or an identifier
 * that is promoted to a variable reference. Chained accesses (`a.b.c`) are reduced one dot at a
 * time.
 * @return NULL on success; the rule silently does not apply if the shape does not match.
 */
compilation_error_t *
parsing_member_access(token_t *operator, parser_memory_t * memory, token_groups_t *groups) {
    assert(operator->type == TOKEN_OPERATOR && operator->text.data[0] == L'.');

    token_t *left_token = operator->left;
    token_t *right_token = operator->right;
    if (left_token == NULL || right_token == NULL) {
        return NULL;
    }
    if (right_token->type != TOKEN_IDENTIFIER) {
        return NULL;
    }

    expression_t *object = NULL;
    if (left_token->type == TOKEN_EXPRESSION) {
        object = (expression_t *)left_token->node;
    } else if (left_token->type == TOKEN_IDENTIFIER) {
        object = create_variable_node(memory->graph, left_token->text);
    } else {
        return NULL;
    }

    expression_t *member = create_member_access_node(memory->graph, object, right_token->text);
    collapse_tokens_to_token(memory, left_token, right_token, TOKEN_EXPRESSION, &member->base);
    return NULL;
}
