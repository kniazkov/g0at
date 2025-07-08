/**
 * @file parsing_declarations.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines reduction rules for ....
 *
 * This file contains the implementation of reduction rules specific to ....
 */
#include <assert.h>

#include "parser.h"
#include "graph/expression.h"
#include "graph/statement.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/vector.h"
#include "resources/messages.h"

#define AVERAGE_NUMBER_OF_VARIABLES_PER_STATEMENT 3

/**
 * @brief Converts ....
 * 
 * Examples where rule applies:
 * 
 * @param keyword The keyword token to process.
 * @param memory Parser memory context.
 * @param groups Token groups (unused in this rule).
 * @return NULL on success, error if conversion fails.
 */
compilation_error_t *parsing_variable_declarations(token_t *keyword, parser_memory_t *memory,
        token_groups_t *groups) {
    assert(keyword->type == TOKEN_VAR);
    compilation_error_t *error = NULL;
    vector_t *vector = NULL;
    if (!keyword->right) {
        error = create_error_from_token(
            memory->tokens,
            keyword,
            get_messages()->expected_var_declaration
        );
        goto cleanup;
    }
    vector = create_vector_ex(AVERAGE_NUMBER_OF_VARIABLES_PER_STATEMENT);
    token_t *token = keyword->right;
    token_t *last_token = token;
    do {
        if (token->type != TOKEN_EXPRESSION || !token->node || (
                token->node->vtbl->type != NODE_VARIABLE &&
                token->node->vtbl->type != NODE_SIMPLE_ASSIGNMENT)) {
            if (token->node) {
                string_value_t str = token->node->vtbl->generate_goat_code(token->node);
                error = create_error_from_token(
                    memory->tokens,
                    keyword,
                    get_messages()->invalid_variable_declaration_syntax,
                    str.data
                );
                if (str.should_free) {
                    FREE(str.data);
                }
            } else {
                error = create_error_from_token(
                    memory->tokens,
                    keyword,
                    get_messages()->invalid_variable_declaration_syntax,
                    token->text
                );
            }
            goto cleanup;
        }
        declarator_t *item = ALLOC(sizeof(declarator_t));
        if (token->node->vtbl->type == NODE_VARIABLE) {
            string_value_t name = token->node->vtbl->generate_goat_code(token->node);
            item->name = name.data;
            item->name_length = name.length;
            item->initial = NULL;
        }
        append_to_vector(vector, item);
        token = token->right;
        if (!token) {
            break;
        }
        if (token->type == TOKEN_COMMA) {
            token = token->right;
        } else if (token->type == TOKEN_OPERATOR) {

        } else {
            break;
        }
    } while(true);
    node_t *declaration = create_variable_declaration_node(
        memory->graph, 
        (declarator_t**)vector->data,
        vector->size
    );
    collapse_tokens_to_token(
        memory->tokens, 
        keyword, 
        last_token,
        TOKEN_STATEMENT, 
        declaration
    );
cleanup:
    if (vector) {
        destroy_vector_ex(vector, FREE);
    }
    return error;
}
