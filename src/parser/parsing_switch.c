/** @file parsing_switch.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Switch labels, comma-separated variants, and isolated branch bodies.
 */
#include "graph/statement_list.h"
#include "graph/switch.h"
#include "lib/arena.h"
#include "parser.h"
#include "resources/messages.h"

compilation_error_t *parsing_flow_keywords(token_t *, parser_memory_t *, token_groups_t *);

static bool flow(token_t *token) {
    return token->type == TOKEN_IF || token->type == TOKEN_FOR || token->type == TOKEN_WHILE
           || token->type == TOKEN_DO || token->type == TOKEN_TRY || token->type == TOKEN_SWITCH;
}

compilation_error_t *
parsing_switch(token_t *token, parser_memory_t *memory, token_groups_t *groups) {
    token_t *header = token->right, *block = header ? header->right : NULL;
    if (!header || header->type != TOKEN_BRACKET_PAIR || header->text.data[0] != L'('
        || header->children.count != 1 || header->children.first->type != TOKEN_EXPRESSION || !block
        || block->type != TOKEN_BRACKET_PAIR || block->text.data[0] != L'{')
        goto invalid;
    switch_branch_t *branches =
        alloc_zeroed_from_arena(memory->graph, (block->children.count + 1) * sizeof(*branches));
    size_t count = 0;
    token_t *part = block->children.first;
    bool otherwise = false;
    while (part) {
        if (otherwise || (part->type != TOKEN_CASE && part->type != TOKEN_DEFAULT))
            goto invalid;
        token_t *start = part;
        otherwise = part->type == TOKEN_DEFAULT;
        switch_branch_t *branch = &branches[count++];
        part = part->right;
        size_t capacity = 0;
        for (token_t *label = part; label && label->type != TOKEN_COLON; label = label->right)
            capacity++;
        branch->labels = alloc_from_arena(memory->graph, (capacity + 1) * sizeof(node_t *));
        if (!otherwise) {
            while (part && part->type == TOKEN_EXPRESSION) {
                branch->labels[branch->count++] = part->node;
                part = part->right;
                if (!part || part->type != TOKEN_COMMA)
                    break;
                part = part->right;
                if (part && part->type == TOKEN_COLON)
                    break;
            }
            if (!branch->count)
                goto invalid;
        }
        if (!part || part->type != TOKEN_COLON)
            goto invalid;
        part = part->right;
        list_t *statements = create_linked_list(memory->graph);
        while (part && part->type != TOKEN_CASE && part->type != TOKEN_DEFAULT) {
            if (flow(part)) {
                token_t *left = part->left;
                compilation_error_t *error = parsing_flow_keywords(part, memory, groups);
                if (error)
                    return error;
                part = left ? left->right : block->children.first;
            }
            node_t *statement = NULL;
            if (part->type == TOKEN_STATEMENT)
                statement = part->node;
            else if (part->type == TOKEN_EXPRESSION)
                statement = (node_t *)create_statement_expression_node(memory->graph,
                                                                       (expression_t *)part->node);
            else if (part->type != TOKEN_SEMICOLON && part->type != TOKEN_COMMA)
                goto invalid;
            if (statement)
                append_item_to_linked_list(statements, (value_t){.ptr = statement});
            part = part->right;
        }
        statement_list_t *body = create_statement_list_node(memory->graph);
        fill_statement_list_node(body, statements);
        body->base.base.position = start->position;
        branch->body = (node_t *)create_statement_expression_node(memory->graph, &body->base);
        branch->body->position = start->position;
    }
    node_t *node = create_switch_node(memory->graph, header->children.first->node, branches, count);
    collapse_tokens_to_token(memory, token, block, TOKEN_STATEMENT, node);
    return NULL;
invalid:
    return create_error_from_token(memory->errors, token, CRITICAL, get_messages()->invalid_switch);
}
