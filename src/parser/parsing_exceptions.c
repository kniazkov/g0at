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
    if (token->type == TOKEN_FINALLY)
        return NULL;
    token_t *binding = token->right;
    if (!binding || binding->type != TOKEN_BRACKET_PAIR || binding->text.data[0] != U'('
        || binding->children.count != 1 || binding->children.first->type != TOKEN_IDENTIFIER)
        return create_error_from_token(memory->errors,
                                       token,
                                       CRITICAL,
                                       get_messages()->invalid_catch_binding);
    remove_token_from_group(binding->children.first);
    remove_token_from_group(binding);
    binding->text = binding->children.first->text;
    if (name_is_reserved_this(binding->text)) {
        return create_error_from_token(memory->errors,
                                       token,
                                       CRITICAL,
                                       get_messages()->reserved_this_name);
    }
    binding->type = TOKEN_CATCH_BINDING;
    return NULL;
}

/** @brief Gives braced, single and empty statements the same lexical scope. */
static statement_list_t *scoped_body(token_t *body, parser_memory_t *memory) {
    if (body->type == TOKEN_EXPRESSION && body->node->vtbl->type == NODE_STATEMENT_LIST)
        return (statement_list_t *)body->node;
    statement_list_t *scope = create_statement_list_node(memory->graph);
    list_t *statements = create_linked_list(memory->graph);
    statement_t *statement = body->type == TOKEN_STATEMENT
                                 ? (statement_t *)body->node
                                 : create_statement_expression_node(memory->graph,
                                                                    body->type == TOKEN_EXPRESSION
                                                                        ? (expression_t *)body->node
                                                                        : NULL);
    append_item_to_linked_list(statements, (value_t){.ptr = statement});
    fill_statement_list_node(scope, statements);
    scope->base.base.position = body->position;
    return scope;
}

static bool is_body(token_t *token) {
    return token
           && (token->type == TOKEN_STATEMENT || token->type == TOKEN_EXPRESSION
               || token->type == TOKEN_SEMICOLON);
}

static token_t *after_body(token_t *body) {
    token_t *next = body->right;
    return body->type != TOKEN_SEMICOLON && next && next->type == TOKEN_SEMICOLON ? next->right
                                                                                  : next;
}

compilation_error_t *parsing_flow_keywords(token_t *, parser_memory_t *, token_groups_t *);

/** @brief A surrounding do statement can request reduction before the ordinary flow pass. */
static compilation_error_t *
prepare_body(token_t *body, parser_memory_t *memory, token_groups_t *groups) {
    if (body
        && (body->type == TOKEN_IF || body->type == TOKEN_FOR || body->type == TOKEN_WHILE
            || body->type == TOKEN_DO || body->type == TOKEN_TRY || body->type == TOKEN_SWITCH))
        return parsing_flow_keywords(body, memory, groups);
    return NULL;
}

/** @brief Reduces try/catch/finally with independent body scopes. */
compilation_error_t *
parsing_try_catch(token_t *token, parser_memory_t *memory, token_groups_t *groups) {
    compilation_error_t *error = prepare_body(token->right, memory, groups);
    if (error)
        return error;
    token_t *body = token->right;
    if (!is_body(body))
        return create_error_from_token(memory->errors,
                                       token,
                                       CRITICAL,
                                       get_messages()->expected_try_statement);
    token_t *kw = after_body(body);
    if (!kw || (kw->type != TOKEN_CATCH && kw->type != TOKEN_FINALLY))
        return create_error_from_token(memory->errors,
                                       token,
                                       CRITICAL,
                                       get_messages()->expected_catch);
    statement_t *statement =
        create_statement_expression_node(memory->graph, &scoped_body(body, memory)->base);
    token_t *last = body;
    if (kw->type == TOKEN_CATCH) {
        token_t *binding = kw->right;
        error = prepare_body(binding->right, memory, groups);
        if (error)
            return error;
        token_t *handler = binding->right;
        if (!is_body(handler))
            return create_error_from_token(memory->errors,
                                           kw,
                                           CRITICAL,
                                           get_messages()->expected_catch_block);
        statement = (statement_t *)create_try_catch_node(memory->graph,
                                                         statement,
                                                         binding->text,
                                                         scoped_body(handler, memory));
        last = handler;
        kw = after_body(handler);
    }
    if (kw && kw->type == TOKEN_FINALLY) {
        error = prepare_body(kw->right, memory, groups);
        if (error)
            return error;
        token_t *cleanup = kw->right;
        if (!is_body(cleanup))
            return create_error_from_token(memory->errors,
                                           kw,
                                           CRITICAL,
                                           get_messages()->expected_finally_statement);
        statement = (statement_t *)create_try_finally_node(memory->graph,
                                                           statement,
                                                           scoped_body(cleanup, memory));
        last = cleanup;
    }
    collapse_tokens_to_token(memory, token, last, TOKEN_STATEMENT, &statement->base);
    return NULL;
}

/** @brief Rejects catch tokens left after try reductions. */
compilation_error_t *
parsing_unmatched_catch(token_t *token, parser_memory_t *memory, token_groups_t *groups) {
    return create_error_from_token(memory->errors,
                                   token,
                                   CRITICAL,
                                   token->type == TOKEN_FINALLY
                                       ? get_messages()->finally_without_try
                                       : get_messages()->catch_without_try);
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
