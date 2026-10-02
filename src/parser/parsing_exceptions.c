/** @file parsing_exceptions.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Reduction rules for try/catch and explicit throw.
 */
#include "graph/statement.h"
#include "graph/statement_list.h"
#include "parser.h"
#include "resources/messages.h"

/** @brief Protects the catch identifier from expression and call reductions. */
compilation_error_t *
preparsing_catch(token_t *token, parser_memory_t *memory, token_groups_t *groups) {
    token_t *binding = token->right;
    if (!binding || binding->type != TOKEN_BRACKET_PAIR || binding->text.data[0] != L'('
        || binding->children.count != 1 || binding->children.first->type != TOKEN_IDENTIFIER)
        return create_error_from_token(memory->errors,
                                       token,
                                       CRITICAL,
                                       get_messages()->invalid_catch_binding);
    remove_token_from_group(binding->children.first);
    remove_token_from_group(binding);
    binding->text = binding->children.first->text;
    binding->type = TOKEN_CATCH_BINDING;
    return NULL;
}

/** @brief Reduces try statement catch (identifier) statement_list. */
compilation_error_t *
parsing_try_catch(token_t *token, parser_memory_t *memory, token_groups_t *groups) {
    token_t *body = token->right;
    if (!body || (body->type != TOKEN_STATEMENT && body->type != TOKEN_EXPRESSION))
        return create_error_from_token(memory->errors,
                                       token,
                                       CRITICAL,
                                       get_messages()->expected_try_statement);
    token_t *kw = body->right;
    if (kw && kw->type == TOKEN_SEMICOLON)
        kw = kw->right;
    if (!kw || kw->type != TOKEN_CATCH)
        return create_error_from_token(memory->errors,
                                       token,
                                       CRITICAL,
                                       get_messages()->expected_catch);
    token_t *binding = kw->right;
    token_t *handler = binding->right;
    if (!handler || handler->type != TOKEN_EXPRESSION
        || handler->node->vtbl->type != NODE_STATEMENT_LIST)
        return create_error_from_token(memory->errors,
                                       kw,
                                       CRITICAL,
                                       get_messages()->expected_catch_block);
    statement_t *statement =
        body->type == TOKEN_STATEMENT
            ? (statement_t *)body->node
            : create_statement_expression_node(memory->graph, (expression_t *)body->node);
    /* Give single statements an insertion site for implicit local declarations. */
    if (body->node->vtbl->type != NODE_STATEMENT_LIST) {
        statement_list_t *scope = create_statement_list_node(memory->graph);
        list_t *statements = create_linked_list(memory->graph);
        append_item_to_linked_list(statements, (value_t){.ptr = statement});
        fill_statement_list_node(scope, statements);
        scope->base.base.position = body->position;
        statement = create_statement_expression_node(memory->graph, &scope->base);
    }
    node_t *node = create_try_catch_node(memory->graph,
                                         statement,
                                         binding->text,
                                         (statement_list_t *)handler->node);
    collapse_tokens_to_token(memory, token, handler, TOKEN_STATEMENT, node);
    return NULL;
}

/** @brief Rejects catch tokens left after try reductions. */
compilation_error_t *
parsing_unmatched_catch(token_t *token, parser_memory_t *memory, token_groups_t *groups) {
    return create_error_from_token(memory->errors,
                                   token,
                                   CRITICAL,
                                   get_messages()->catch_without_try);
}

/** @brief Reduces a throw with a required value expression. */
compilation_error_t *
parsing_throw(token_t *token, parser_memory_t *memory, token_groups_t *groups) {
    if (!token->right || token->right->type != TOKEN_EXPRESSION)
        return create_error_from_token(memory->errors,
                                       token,
                                       CRITICAL,
                                       get_messages()->expected_throw_value);
    collapse_tokens_to_token(memory,
                             token,
                             token->right,
                             TOKEN_STATEMENT,
                             create_throw_node(memory->graph, (expression_t *)token->right->node));
    return NULL;
}
