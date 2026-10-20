/**
 * @file parsing_declarations.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines reduction rules for variable and constant declarations.
 */
#include "graph/assignment.h"
#include "graph/declarations.h"
#include "graph/statement.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/vector.h"
#include "parser.h"
#include "resources/messages.h"

#include <assert.h>

#define AVERAGE_NUMBER_OF_VARIABLES_PER_STATEMENT 3

/** @brief Normalizes a single visibility prefix to a var/const declaration keyword. */
compilation_error_t *
parsing_access_modifier(token_t *token, parser_memory_t *memory, token_groups_t *groups) {
    token_t *next = token->right;
    if (!next || (next->type >= TOKEN_PUBLIC && next->type <= TOKEN_PRIVATE)
        || (token->left && (token->left->type == TOKEN_VAR || token->left->type == TOKEN_CONST)))
        return create_error_from_token(memory->errors,
                                       token,
                                       CRITICAL,
                                       get_messages()->invalid_access_modifier);
    token->access_modifier = token->type == TOKEN_PUBLIC      ? ACCESS_PUBLIC
                             : token->type == TOKEN_PROTECTED ? ACCESS_PROTECTED
                                                              : ACCESS_PRIVATE;
    remove_token_from_group(token);
    token->type = next->type == TOKEN_VAR ? TOKEN_VAR : TOKEN_CONST;
    if (next->type == TOKEN_VAR || next->type == TOKEN_CONST)
        remove_token(next);
    append_token_to_group(token->type == TOKEN_VAR ? &groups->var_keywords
                                                   : &groups->const_keywords,
                          token);
    return NULL;
}

/**
 * @brief Processes variable declaration statements.
 * @return NULL on successful parsing, or a compilation error if parsing fails.
 */
compilation_error_t *
parsing_variable_declarations(token_t *keyword, parser_memory_t *memory, token_groups_t *groups) {
    assert(keyword->type == TOKEN_VAR);
    compilation_error_t *error = NULL;
    vector_t *vector = NULL;
    if (!keyword->right) {
        error = create_error_from_token(memory->errors,
                                        keyword,
                                        CRITICAL,
                                        get_messages()->expected_var_declaration);
        goto cleanup;
    }
    vector = create_vector_ex(AVERAGE_NUMBER_OF_VARIABLES_PER_STATEMENT);
    token_t *token = keyword->right;
    token_t *last_token;
    do {
        last_token = token;
        if (token->type != TOKEN_EXPRESSION || !token->node
            || (token->node->vtbl->type != NODE_VARIABLE
                && token->node->vtbl->type != NODE_SIMPLE_ASSIGNMENT)) {
            goto invalid_declaration;
        }
        declarator_spec_t *item = NULL;
        if (token->node->vtbl->type == NODE_VARIABLE) {
            item = create_declarator_from_variable(token->node);
        } else {
            item = create_declarator_from_simple_assignment(token->node);
            if (item == NULL) {
                goto invalid_declaration;
            }
        }
        assert(item != NULL);
        if (name_is_reserved_this(item->name)) {
            error = create_error_from_token(memory->errors,
                                            keyword,
                                            CRITICAL,
                                            get_messages()->reserved_this_name);
            FREE(item);
            goto cleanup;
        }
        append_to_vector(vector, item);
        token = token->right;
        if (!token) {
            break;
        }
        if (token->type == TOKEN_COMMA) {
            if (!token->right) {
                error = create_error_from_token(memory->errors,
                                                token,
                                                CRITICAL,
                                                get_messages()->expected_var_after_comma);
                goto cleanup;
            }
            token = token->right;
        } else {
            break;
        }
    } while (true);
    node_t *declaration = create_variable_declaration_node(memory->graph,
                                                           (declarator_spec_t **)vector->data,
                                                           vector->size);
    set_declaration_modifier(declaration, keyword->access_modifier);
    collapse_tokens_to_token(memory, keyword, last_token, TOKEN_STATEMENT, declaration);
    goto cleanup;

invalid_declaration: {
    string_value_t last_token_str = token_to_string(last_token);
    error = create_error_from_token(memory->errors,
                                    keyword,
                                    CRITICAL,
                                    get_messages()->invalid_var_declaration_syntax,
                                    last_token_str.data);
    FREE_STRING(last_token_str);
}

cleanup:
    if (vector) {
        destroy_vector_ex(vector, FREE);
    }
    return error;
}

/**
 * @brief Processes constant declaration statements.
 * @return NULL on successful parsing, or a compilation error if parsing fails.
 */
compilation_error_t *
parsing_constant_declarations(token_t *keyword, parser_memory_t *memory, token_groups_t *groups) {
    assert(keyword->type == TOKEN_CONST);
    /* Leave the declaration-free binding for the for-in header reducer. */
    if (keyword->right && keyword->right->right && keyword->right->right->type == TOKEN_IN)
        return NULL;
    compilation_error_t *error = NULL;
    vector_t *vector = NULL;
    if (!keyword->right) {
        error = create_error_from_token(memory->errors,
                                        keyword,
                                        CRITICAL,
                                        get_messages()->expected_const_declaration);
        goto cleanup;
    }
    vector = create_vector_ex(AVERAGE_NUMBER_OF_VARIABLES_PER_STATEMENT);
    token_t *token = keyword->right;
    token_t *last_token;
    do {
        last_token = token;
        if (token->type != TOKEN_EXPRESSION || !token->node
            || token->node->vtbl->type != NODE_SIMPLE_ASSIGNMENT) {
            goto invalid_declaration;
        }
        declarator_spec_t *item = create_declarator_from_simple_assignment(token->node);
        if (item == NULL) {
            goto invalid_declaration;
        }
        if (name_is_reserved_this(item->name)) {
            error = create_error_from_token(memory->errors,
                                            keyword,
                                            CRITICAL,
                                            get_messages()->reserved_this_name);
            FREE(item);
            goto cleanup;
        }
        append_to_vector(vector, item);
        token = token->right;
        if (!token) {
            break;
        }
        if (token->type == TOKEN_COMMA) {
            if (!token->right) {
                error = create_error_from_token(memory->errors,
                                                token,
                                                CRITICAL,
                                                get_messages()->expected_const_after_comma);
                goto cleanup;
            }
            token = token->right;
        } else {
            break;
        }
    } while (true);
    node_t *declaration = create_constant_declaration_node(memory->graph,
                                                           (declarator_spec_t **)vector->data,
                                                           vector->size);
    set_declaration_modifier(declaration, keyword->access_modifier);
    collapse_tokens_to_token(memory, keyword, last_token, TOKEN_STATEMENT, declaration);
    goto cleanup;

invalid_declaration: {
    string_value_t last_token_str = token_to_string(last_token);
    error = create_error_from_token(memory->errors,
                                    keyword,
                                    CRITICAL,
                                    get_messages()->invalid_const_declaration_syntax,
                                    last_token_str.data);
    FREE_STRING(last_token_str);
}

cleanup:
    if (vector) {
        destroy_vector_ex(vector, FREE);
    }
    return error;
}
