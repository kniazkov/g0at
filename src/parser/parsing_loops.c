/** @file parsing_loops.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Pre-test and post-test loops, with optional statement separators.
 */
#include "graph/loop.h"
#include "graph/statement_list.h"
#include "parser.h"
#include "resources/messages.h"

static statement_t *body_statement(token_t *body, parser_memory_t *memory) {
    statement_t *statement = body->type == TOKEN_STATEMENT
                                 ? (statement_t *)body->node
                                 : create_statement_expression_node(memory->graph,
                                                                    body->type == TOKEN_EXPRESSION
                                                                        ? (expression_t *)body->node
                                                                        : NULL);
    if (body->type != TOKEN_EXPRESSION || body->node->vtbl->type != NODE_STATEMENT_LIST) {
        statement_list_t *scope = create_statement_list_node(memory->graph);
        list_t *items = create_linked_list(memory->graph);
        append_item_to_linked_list(items, (value_t){.ptr = statement});
        fill_statement_list_node(scope, items);
        scope->base.base.position = body->position;
        statement = create_statement_expression_node(memory->graph, &scope->base);
    }
    return statement;
}

compilation_error_t *
parsing_while(token_t *token, parser_memory_t *memory, token_groups_t *groups) {
    bool post = token->type == TOKEN_DO;
    token_t *body = post ? token->right : token->right ? token->right->right : NULL;
    token_t *header = token->right, *last = body;
    if (!body
        || (body->type != TOKEN_EXPRESSION && body->type != TOKEN_STATEMENT
            && body->type != TOKEN_SEMICOLON))
        goto invalid;
    if (post) {
        token_t *tail = body->right;
        if (tail && tail->type == TOKEN_SEMICOLON)
            tail = tail->right;
        if (!tail || tail->type != TOKEN_WHILE)
            goto invalid;
        header = tail->right;
        last = header;
    }
    if (!header || header->type != TOKEN_BRACKET_PAIR || header->text.data[0] != L'('
        || header->children.count != 1 || header->children.first->type != TOKEN_EXPRESSION)
        goto invalid;
    node_t *node = create_while_node(memory->graph,
                                     (expression_t *)header->children.first->node,
                                     body_statement(body, memory),
                                     post);
    collapse_tokens_to_token(memory, token, last, TOKEN_STATEMENT, node);
    return NULL;
invalid:
    return create_error_from_token(memory->errors, token, CRITICAL, get_messages()->invalid_loop);
}

compilation_error_t *
parsing_loop_jump(token_t *token, parser_memory_t *memory, token_groups_t *groups) {
    node_t *node = create_loop_jump_node(memory->graph, token->type == TOKEN_CONTINUE);
    collapse_tokens_to_token(memory, token, token, TOKEN_STATEMENT, node);
    return NULL;
}
