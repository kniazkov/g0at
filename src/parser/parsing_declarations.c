/**
 * @file parsing_declarations.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines reduction rules for variable and constant declarations.
 *
 * This file contains the implementation of reduction rules specific to processing
 * variable and constant declarations in the source code. It handles the conversion
 * of declaration statements into appropriate AST nodes.
 */
#include <assert.h>

#include "parser.h"
#include "graph/assignment.h"
#include "graph/statement.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/vector.h"
#include "resources/messages.h"

#define AVERAGE_NUMBER_OF_VARIABLES_PER_STATEMENT 3

/**
 * @brief Processes variable declaration statements.
 * 
 * Converts a sequence of tokens starting with 'var' into a variable declaration AST node.
 * Handles both simple declarations and initialized variables, including multiple
 * declarations separated by commas.
 *
 * @param keyword The 'var' keyword token that starts the declaration.
 * @param memory Parser memory context containing tokens and graph.
 * @param groups Token groups (unused in this implementation).
 * @return NULL on successful parsing, or a compilation error if parsing fails.
 *
 * @note Handles cases like:
 *       - var x
 *       - var x = 1
 *       - var x, y = 2, z
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
    token_t *last_token;
    do {
        last_token = token;
        if (token->type != TOKEN_EXPRESSION || !token->node || (
                token->node->vtbl->type != NODE_VARIABLE &&
                token->node->vtbl->type != NODE_SIMPLE_ASSIGNMENT)) {
            goto invalid_declaration;
        }
        declarator_t *item = NULL;
        if (token->node->vtbl->type == NODE_VARIABLE) {
            item = create_declarator_from_variable(token->node);
        } else {
            item = create_declarator_from_simple_assignment(token->node);
            if (item == NULL) {
                goto invalid_declaration;
            }
        }
        assert(item != NULL);
        append_to_vector(vector, item);
        token = token->right;
        if (!token) {
            break;
        }
        if (token->type == TOKEN_COMMA) {
            if (!token->right) {
                error = create_error_from_token(
                    memory->tokens,
                    token,
                    get_messages()->expected_var_after_comma
                );
                goto cleanup;
            }
            token = token->right;
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
    goto cleanup;

invalid_declaration:
    {
        string_value_t last_token_str = token_to_string(last_token);
        error = create_error_from_token(
            memory->tokens,
            keyword,
            get_messages()->invalid_variable_declaration_syntax,
            last_token_str.data
        );
        if (last_token_str.should_free) {
            FREE(last_token_str.data);
        }
    }

cleanup:
    if (vector) {
        destroy_vector_ex(vector, FREE);
    }
    return error;
}
