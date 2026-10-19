/** @file parsing_for.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Reduction of C-style loop headers and bodies.
 */
#include "graph/expression.h"
#include "graph/loop.h"
#include "graph/statement.h"
#include "graph/statement_list.h"
#include "graph/variable.h"
#include "parser.h"
#include "resources/messages.h"

/** @brief Wraps an expression or an empty slot as a statement. */
static statement_t *as_statement(token_t *token, parser_memory_t *memory) {
    if (token && token->type == TOKEN_STATEMENT)
        return (statement_t *)token->node;
    return create_statement_expression_node(
        memory->graph,
        token && token->type == TOKEN_EXPRESSION ? (expression_t *)token->node : NULL);
}

/** @brief Parses exactly two separators and three optional header clauses. */
compilation_error_t *parsing_for(token_t *token, parser_memory_t *memory, token_groups_t *groups) {
    token_t *header = token->right;
    if (!header || header->type != TOKEN_BRACKET_PAIR || header->text.data[0] != L'(')
        goto invalid;
    token_t *binding = header->children.first;
    if (binding && binding->type == TOKEN_CONST)
        binding = binding->right;
    bool collection = binding && binding->right && binding->right->type == TOKEN_IN;
    token_t *source = collection ? binding->right->right : NULL;
    if (collection
        && (binding->type != TOKEN_EXPRESSION || !binding->node
            || binding->node->vtbl->type != NODE_VARIABLE || !source
            || source->type != TOKEN_EXPRESSION || source->right))
        goto invalid;
    if (collection && !wcscmp(((variable_t *)binding->node)->name.data, L"this"))
        return create_error_from_token(memory->errors,
                                       binding,
                                       CRITICAL,
                                       get_messages()->reserved_this_name);
    token_t *slots[3] = {0};
    size_t slot = 0;
    for (token_t *part = collection ? NULL : header->children.first; part; part = part->right) {
        if (part->type == TOKEN_SEMICOLON) {
            if (++slot > 2)
                goto invalid;
        } else {
            if (slots[slot])
                goto invalid;
            if (part->type != TOKEN_EXPRESSION
                && !(slot == 0 && part->type == TOKEN_STATEMENT
                     && (part->node->vtbl->type == NODE_VARIABLE_DECLARATION
                         || part->node->vtbl->type == NODE_CONSTANT_DECLARATION)))
                goto invalid;
            slots[slot] = part;
        }
    }
    if (!collection && slot != 2)
        goto invalid;
    token_t *body = header->right;
    if (!body
        || (body->type != TOKEN_STATEMENT && body->type != TOKEN_EXPRESSION
            && body->type != TOKEN_SEMICOLON))
        return create_error_from_token(memory->errors,
                                       header,
                                       CRITICAL,
                                       get_messages()->expected_statement_after_for);
    expression_t *condition =
        slots[1] ? (expression_t *)slots[1]->node : (expression_t *)create_true_node(memory->graph);
    if (!slots[1])
        condition->base.position = header->position;
    statement_t *body_statement = as_statement(body, memory);
    if (body->type != TOKEN_EXPRESSION || body->node->vtbl->type != NODE_STATEMENT_LIST) {
        statement_list_t *scope = create_statement_list_node(memory->graph);
        list_t *statements = create_linked_list(memory->graph);
        append_item_to_linked_list(statements, (value_t){.ptr = body_statement});
        fill_statement_list_node(scope, statements);
        scope->base.base.position = body->position;
        body_statement = create_statement_expression_node(memory->graph, &scope->base);
    }
    node_t *node = collection ? create_for_in_node(memory->graph,
                                                   ((variable_t *)binding->node)->name,
                                                   (expression_t *)source->node,
                                                   body_statement)
                              : create_for_node(memory->graph,
                                                as_statement(slots[0], memory),
                                                condition,
                                                as_statement(slots[2], memory),
                                                body_statement);
    collapse_tokens_to_token(memory, token, body, TOKEN_STATEMENT, node);
    return NULL;
invalid:
    return create_error_from_token(memory->errors,
                                   token,
                                   CRITICAL,
                                   get_messages()->invalid_for_header);
}
