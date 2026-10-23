/** @file parsing_member_access.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Reduces mixed member, index and call postfix chains.
 */
#include "graph/array.h"
#include "graph/expression.h"
#include "graph/index_access.h"
#include "graph/member_access.h"
#include "parser.h"

compilation_error_t *
parsing_member_access(token_t *op, parser_memory_t *memory, token_groups_t *groups) {
    token_t *left = op->left;

    /* Groups contain nested brackets in closing order. Resolve the preceding suffix first. */
    if (left
        && (left->type == TOKEN_BRACKET_PAIR
            || (left->type == TOKEN_IDENTIFIER && left->left && left->left->type == TOKEN_OPERATOR
                && left->left->text.data[0] == U'.'))) {
        token_t *previous = left->type == TOKEN_IDENTIFIER ? left->left : left;
        compilation_error_t *error = parsing_member_access(previous, memory, groups);
        if (error)
            return error;
        left = op->left;
    }
    if (op->type == TOKEN_BRACKET_PAIR && op->text.data[0] == '['
        && (!left || (left->type != TOKEN_EXPRESSION && left->type != TOKEN_IDENTIFIER))) {
        bool prototypes = op->right && op->right->type == TOKEN_OPERATOR
                          && !goat_strcmp(op->right->text.data, U"->");
        node_t *node = prototypes ? create_prototype_list_node(memory->graph)
                                  : create_array_node(memory->graph);
        op->type = prototypes ? TOKEN_PROTOTYPE_ITEMS : TOKEN_ARRAY_ITEMS;
        op->node = node;
        collapse_tokens_to_token(memory, op, op, TOKEN_EXPRESSION, node);
        append_token_to_group(&groups->function_arguments, op);
        return NULL;
    }
    if (!left)
        return NULL;
    expression_t *object;
    if (left->type == TOKEN_EXPRESSION)
        object = (expression_t *)left->node;
    else if (left->type == TOKEN_IDENTIFIER)
        object = create_variable_node(memory->graph, left->text);
    else
        return NULL;
    if (op->type == TOKEN_BRACKET_PAIR) {
        bool index = op->text.data[0] == '[';
        if (!index && op->text.data[0] != '(')
            return NULL;
        node_t *node = index ? create_index_access_node(memory->graph, object)
                             : create_function_call_node_without_args(memory->graph, object);
        op->type = index ? TOKEN_INDEX_ARGS : TOKEN_FCALL_ARGS;
        op->node = node;
        collapse_tokens_to_token(memory, left, op, TOKEN_EXPRESSION, node);
        append_token_to_group(&groups->function_arguments, op);
        return NULL;
    }
    if (op->type != TOKEN_OPERATOR || op->text.data[0] != U'.')
        return NULL;
    token_t *right = op->right;
    if (!right || right->type != TOKEN_IDENTIFIER)
        return NULL;
    expression_t *member = create_member_access_node(memory->graph, object, right->text);
    token_t *args = right->right;
    if (args && args->type == TOKEN_BRACKET_PAIR && args->text.data[0] == '(') {
        node_t *call = create_function_call_node_without_args(memory->graph, member);
        args->type = TOKEN_FCALL_ARGS;
        args->node = call;
        collapse_tokens_to_token(memory, left, args, TOKEN_EXPRESSION, call);
        append_token_to_group(&groups->function_arguments, args);
    } else {
        collapse_tokens_to_token(memory, left, right, TOKEN_EXPRESSION, &member->base);
    }
    return NULL;
}
