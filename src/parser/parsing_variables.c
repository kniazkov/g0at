/**
 * @file parsing_variables.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines reduction rules for creating variable reference nodes.
 *
 * This file contains the implementation of reduction rules specific to variable references.
 * These rules are applied during the parsing process to create new tokens and 
 * abstract syntax tree (AST) nodes representing variable access. The rules define how
 * standalone identifiers should be transformed into variable reference expressions.
 */
#include <assert.h>

#include "parser.h"
#include "graph/expression.h"
#include "lib/arena.h"
#include "resources/messages.h"

/**
 * @brief Converts single (isolated) identifiers or identifiers surrounded by operators
 * or other symbols into variable references.
 * 
 * Examples where rule applies:
 * - `+ var +`   → converts `var`
 * - `var + 1` → converts `var` (if at start)
 * - `1 + var` → converts `var` (if at end)
 * 
 * Does NOT match:
 * - Function calls (`func()`)
 * - Member access (`obj.prop`)
 * 
 * @param identifier The identifier token to process.
 * @param memory Parser memory context.
 * @param groups Token groups (unused in this rule).
 * @return NULL on success, error if conversion fails.
 */
compilation_error_t *parsing_single_identifiers(token_t *identifier, parser_memory_t *memory,
        token_groups_t *groups) {
    assert(identifier->type == TOKEN_IDENTIFIER);
    
    if (identifier->left != NULL) {
        bool valid_left = identifier->left->type == TOKEN_OPERATOR 
                       || identifier->left->type == TOKEN_EXPRESSION
                       || identifier->left->type == TOKEN_COMMA
                       || identifier->left->type == TOKEN_SEMICOLON
                       || identifier->left->type == TOKEN_VAR
                       || identifier->left->type == TOKEN_CONST
                       ;
        if (!valid_left) {
            return NULL;
        }
    }
    
    if (identifier->right != NULL) {
        bool valid_right = identifier->right->type == TOKEN_OPERATOR 
                        || identifier->right->type == TOKEN_EXPRESSION 
                        || identifier->right->type == TOKEN_IDENTIFIER
                        || identifier->right->type == TOKEN_COMMA
                        || identifier->right->type == TOKEN_SEMICOLON
                        || identifier->right->type == TOKEN_VAR
                        || identifier->right->type == TOKEN_CONST
                        ;
        if (!valid_right) {
            return NULL;
        }
    }
    
    node_t *variable = (node_t*)create_variable_node(memory->graph, identifier->text);
    
    collapse_tokens_to_token(
        memory->tokens, 
        identifier, 
        identifier,
        TOKEN_EXPRESSION, 
        variable
    );
    
    return NULL;
}
