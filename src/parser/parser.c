/**
 * @file parser.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implements the functions for the parser.
 */

#include "parser.h"

#include "graph/node.h"
#include "lib/memory/arena.h"
#include "resources/messages.h"
#include "scanner/scanner.h"
#include "scanner/token.h"

#include <assert.h>
#include <memory.h>
#include <stdbool.h>

compilation_error_t *parsing_loop_jump(token_t *, parser_memory_t *, token_groups_t *);
compilation_error_t *parsing_access_modifier(token_t *, parser_memory_t *, token_groups_t *);

compilation_error_t *parsing_inheritance(token_t *, parser_memory_t *, token_groups_t *);

/** @brief Reduces postfix updates before prefix updates. */
compilation_error_t *parsing_postfix_updates(token_t *, parser_memory_t *, token_groups_t *);
compilation_error_t *parsing_prefix_updates(token_t *, parser_memory_t *, token_groups_t *);

/** @brief Rule for handling comparison operators, followed by expressions on both sides. */
compilation_error_t *
parsing_comparison_operators(token_t *operator, parser_memory_t * memory, token_groups_t *groups);

/** @brief Rule for handling an additive operator (`+` or `-`), followed by expressions on both
 * sides. */
compilation_error_t *
parsing_additive_operators(token_t *operator, parser_memory_t * memory, token_groups_t *groups);

/**
 * @brief Rule for handling a multiplicative operator (`*`, `/` or `%`), followed by expressions on
 * both sides.
 */
compilation_error_t *parsing_multiplicative_operators(token_t *operator,
                                                      parser_memory_t * memory,
                                                      token_groups_t *groups);

/** @brief Rule for handling a power operator (`**`), followed by expressions on both sides. */
compilation_error_t *
parsing_power_operators(token_t *operator, parser_memory_t * memory, token_groups_t *groups);

/**
 * @brief Rule for handling an assignment operator, with assignable expression on the left side and
 * expression on the right side.
 */
compilation_error_t *
parsing_assignment_operators(token_t *operator, parser_memory_t * memory, token_groups_t *groups);

/** @brief Rule for handling an identifier followed by parentheses (function call). */
compilation_error_t *parsing_identifier_and_parentheses(token_t *identifier,
                                                        parser_memory_t *memory,
                                                        token_groups_t *groups);

/** @brief Rule for handling function calls arguments. */
compilation_error_t *
parsing_function_call_args(token_t *identifier, parser_memory_t *memory, token_groups_t *groups);

/** @brief Rule for handling single (isolated) identifiers as variables. */
compilation_error_t *
parsing_single_identifiers(token_t *identifier, parser_memory_t *memory, token_groups_t *groups);

/** @brief Rule for handling member access (an expression followed by `.name`). */
compilation_error_t *
parsing_member_access(token_t *operator, parser_memory_t * memory, token_groups_t *groups);

/** @brief Rule for handling variable declarations. */
compilation_error_t *
parsing_variable_declarations(token_t *keyword, parser_memory_t *memory, token_groups_t *groups);

/** @brief Rule for handling constant declarations. */
compilation_error_t *
parsing_constant_declarations(token_t *keyword, parser_memory_t *memory, token_groups_t *groups);

/** @brief Rule for handling scope blocks (curly brace pairs) and functon declarations. */
compilation_error_t *
parsing_scopes_and_functions(token_t *token, parser_memory_t *memory, token_groups_t *groups);

/** @brief Rule for handling statements within a statement list. */
compilation_error_t *
parsing_statement_list_bodies(token_t *token, parser_memory_t *memory, token_groups_t *groups);

/** @brief Rule for handling statements within a function. */
compilation_error_t *
parsing_function_bodies(token_t *token, parser_memory_t *memory, token_groups_t *groups);

/** @brief Rule for handling `return` statements. */
compilation_error_t *
parsing_returns(token_t *token, parser_memory_t *memory, token_groups_t *groups);

/** @brief Rule for handling `if-else`, `for`, `do-while`, `while` statements. */
compilation_error_t *
parsing_flow_keywords(token_t *token, parser_memory_t *memory, token_groups_t *groups);

compilation_error_t *parsing_unary_operators(token_t *, parser_memory_t *, token_groups_t *);

compilation_error_t *preparsing_catch(token_t *, parser_memory_t *, token_groups_t *);
compilation_error_t *parsing_unmatched_catch(token_t *, parser_memory_t *, token_groups_t *);
compilation_error_t *parsing_throw(token_t *, parser_memory_t *, token_groups_t *);

/** @brief Rule for handling `else` keywords. */
compilation_error_t *
parsing_else_keywords(token_t *token, parser_memory_t *memory, token_groups_t *groups);

/**
 * @brief Rule for processing an expression in parentheses - identifies it as such and changes it in
 * the token chain.
 */
compilation_error_t *preparsing_parenthesized_expressions(token_t *token,
                                                          parser_memory_t *memory,
                                                          token_groups_t *groups);

/** @brief Rule for final processing of expressions in parentheses. */
compilation_error_t *
parsing_parenthesized_expressions(token_t *token, parser_memory_t *memory, token_groups_t *groups);

/**
 * @brief Scans and analyzes tokens for balanced brackets, transforming nested brackets into a
 * special token.
 * @return A `compilation_error_t` pointer if an error is detected (e.g., mismatched brackets), or
 * NULL if no errors.
 */
static compilation_error_t *scan_and_analyze_for_brackets(parser_memory_t *memory,
                                                          scanner_t *scan,
                                                          token_list_t *list,
                                                          const token_t *opening_token,
                                                          const token_t **closing_token,
                                                          token_groups_t *groups) {
    const token_t *previous = opening_token;
    while (true) {
        token_t *token = get_token(scan);
        if (token == NULL) {
            if (opening_token == NULL) {
                return NULL; // no opening bracket - no error
            }
            compilation_error_t *error =
                create_error_from_token(memory->errors,
                                        opening_token,
                                        CRITICAL,
                                        get_messages()->unclosed_opening_bracket,
                                        opening_token->text.data[0]);
            error->position = create_position_range(memory->positions,
                                                    opening_token->position->begin,
                                                    previous->position->end);
            return error;
        }
        if (token->type == TOKEN_ERROR) {
            return create_error_from_token(memory->errors,
                                           token,
                                           CRITICAL,
                                           U"%s",
                                           token->text.data);
        }
        if (token->type == TOKEN_BRACKET) {
            gchar_t bracket = token->text.data[0];
            if (bracket == U'(' || bracket == U'{' || bracket == '[') {
                token_t *pair = (token_t *)alloc_zeroed_from_arena(memory->tokens, sizeof(token_t));
                pair->type = TOKEN_BRACKET_PAIR;
                compilation_error_t *error = scan_and_analyze_for_brackets(memory,
                                                                           scan,
                                                                           &pair->children,
                                                                           token,
                                                                           &previous,
                                                                           groups);
                if (error != NULL) {
                    return error;
                }
                pair->position = create_position_range(memory->positions,
                                                       token->position->begin,
                                                       previous->position->end);
                gchar_t *text = (gchar_t *)alloc_from_arena(memory->tokens, sizeof(gchar_t) * 3);
                text[0] = bracket;
                text[1] = previous->text.data[0];
                text[2] = U'\0';
                pair->text = (string_view_t){text, 2};
                append_token_to_neighbors(list, pair);
                if (bracket == '(') {
                    append_token_to_group(&groups->unprocessed_parenthesized_expressions, pair);
                } else if (bracket == '[') {
                    append_token_to_group(&groups->member_access_operators, pair);
                } else if (bracket == '{') {
                    append_token_to_group(&groups->scope_blocks, pair);
                }
            } else {
                *closing_token = token;
                if (opening_token == NULL) {
                    return create_error_from_token(memory->errors,
                                                   token,
                                                   CRITICAL,
                                                   get_messages()->missing_opening_bracket,
                                                   bracket);
                }
                gchar_t opening_bracket = U'\0';
                if (bracket == U')') {
                    opening_bracket = U'(';
                } else if (bracket == U']') {
                    opening_bracket = U'[';
                } else if (bracket == U'}') {
                    opening_bracket = U'{';
                }
                assert(opening_bracket != U'\0');
                if (opening_token->text.data[0] != opening_bracket) {
                    compilation_error_t *error =
                        create_error_from_token(memory->errors,
                                                opening_token,
                                                CRITICAL,
                                                get_messages()->brackets_do_not_match,
                                                bracket,
                                                opening_token->text.data[0]);
                    error->position = create_position_range(memory->positions,
                                                            opening_token->position->begin,
                                                            token->position->end);
                    return error;
                }
                return NULL;
            }
        } else {
            append_token_to_neighbors(list, token);
            previous = token;
        }
    }
    return NULL;
}

/** @brief Applies a reduction rule to the token list from the first to the last token. */
static compilation_error_t *apply_reduction_rule_forward(token_list_t *list,
                                                         reduce_rule_t rule,
                                                         parser_memory_t *memory,
                                                         token_groups_t *groups,
                                                         compilation_error_t *error) {
    token_t *token = list->first;
    while (token != NULL) {
        token_t *next = token->next_in_group;
        compilation_error_t *new_error = rule(token, memory, groups);
        if (new_error != NULL) {
            new_error->next = error;
            error = new_error;
            if (error->severity == CRITICAL) {
                break;
            }
        }
        token = next;
    }
    return error;
}

/**
 * @brief Applies a reduction rule to the token list from the last to the first token.
 *
 * Traverses the token list in reverse order, starting from the last token and moving to the first,
 * applying the provided reduction rule to each token.
 */
static compilation_error_t *apply_reduction_rule_backward(token_list_t *list,
                                                          reduce_rule_t rule,
                                                          parser_memory_t *memory,
                                                          token_groups_t *groups,
                                                          compilation_error_t *error) {
    token_t *token = list->last;
    while (token != NULL) {
        token_t *previous = token->previous_in_group;
        compilation_error_t *new_error = rule(token, memory, groups);
        if (new_error != NULL) {
            new_error->next = error;
            error = new_error;
            if (error->severity == CRITICAL) {
                break;
            }
        }
        token = previous;
    }
    return error;
}

compilation_error_t *process_brackets(parser_memory_t *memory,
                                      scanner_t *scan,
                                      token_list_t *tokens,
                                      token_groups_t *groups) {
    memset(tokens, 0, sizeof(token_list_t));
    const token_t *last_token;
    compilation_error_t *error =
        scan_and_analyze_for_brackets(memory, scan, tokens, NULL, &last_token, groups);
    return error;
}

token_t *collapse_tokens_to_token(parser_memory_t *memory,
                                  token_t *first,
                                  token_t *last,
                                  token_type_t type,
                                  node_t *node) {
    token_t *new_token = (token_t *)alloc_zeroed_from_arena(memory->tokens, sizeof(token_t));
    new_token->type = type;
    new_token->node = node;

    position_range_t *position;
    if (first == last) {
        position = first->position;
    } else {
        position =
            create_position_range(memory->positions, first->position->begin, last->position->end);
    }
    new_token->position = position;
    node->position = position;

    token_t *old_token = first;
    while (old_token != last) {
        token_t *next = old_token->right;
        remove_token(old_token);
        old_token = next;
    }
    replace_token(old_token, new_token);
    return new_token;
}

statement_list_processing_result_t process_statement_list(parser_memory_t *memory,
                                                          token_list_t *tokens) {
    statement_list_processing_result_t result = {0};
    result.list = create_linked_list(memory->graph);

    token_t *token = tokens->first;
    while (token != NULL) {
        if (token->type == TOKEN_STATEMENT) {
            append_item_to_linked_list(result.list, (value_t){.ptr = token->node});
        } else if (token->type == TOKEN_EXPRESSION) {
            statement_t *stmt =
                create_statement_expression_node(memory->graph, (expression_t *)token->node);
            append_item_to_linked_list(result.list, (value_t){.ptr = stmt});
        } else if (token->type != TOKEN_COMMA && token->type != TOKEN_SEMICOLON) {
            result.error = create_error_from_token(memory->errors,
                                                   token,
                                                   CRITICAL,
                                                   get_messages()->not_a_statement,
                                                   token->text);
            break;
        }
        token = token->right;
    }
    return result;
}

/** @brief Applies a reduction rule forward and checks for critical errors */
#define APPLY_FORWARD(group_name, rule_func)                                                       \
    do {                                                                                           \
        error =                                                                                    \
            apply_reduction_rule_forward(&groups->group_name, rule_func, memory, groups, error);   \
        if (error != NULL && error->severity == CRITICAL) {                                        \
            return error;                                                                          \
        }                                                                                          \
    } while (0)

/** @brief Applies a reduction rule backward and checks for critical errors */
#define APPLY_BACKWARD(group_name, rule_func)                                                      \
    do {                                                                                           \
        error =                                                                                    \
            apply_reduction_rule_backward(&groups->group_name, rule_func, memory, groups, error);  \
        if (error != NULL && error->severity == CRITICAL) {                                        \
            return error;                                                                          \
        }                                                                                          \
    } while (0)

/**
 * @brief Collects AST nodes from a token group into a linked list.
 *
 * Iterates over all tokens in the given token group and extracts non-null `node` pointers.
 * @return A linked list containing all non-null AST nodes from the token group.
 */
static list_t *collect_nodes_from_group(token_list_t *tokens, arena_t *arena) {
    list_t *nodes = create_linked_list(arena);
    token_t *token = tokens->first;
    while (token) {
        if (token->node) {
            append_item_to_linked_list(nodes, (value_t){.ptr = token->node});
        }
        token = token->next_in_group;
    }
    return nodes;
}

compilation_error_t *parsing_logic_operators(token_t *, parser_memory_t *, token_groups_t *);

compilation_error_t *
apply_reduction_rules(token_groups_t *groups, parser_memory_t *memory, parsing_result_t *result) {
    compilation_error_t *error = NULL;
    APPLY_FORWARD(catch_keywords, preparsing_catch);
    APPLY_FORWARD(access_keywords, parsing_access_modifier);
    APPLY_FORWARD(scope_blocks, parsing_scopes_and_functions);
    result->functions = collect_nodes_from_group(&groups->function_objects, memory->graph);
    APPLY_FORWARD(identifiers, parsing_identifier_and_parentheses);
    APPLY_FORWARD(unprocessed_parenthesized_expressions, preparsing_parenthesized_expressions);
    APPLY_FORWARD(member_access_operators, parsing_member_access);
    APPLY_FORWARD(identifiers, parsing_single_identifiers);
    APPLY_FORWARD(update_operators, parsing_postfix_updates);
    APPLY_BACKWARD(update_operators, parsing_prefix_updates);
    APPLY_BACKWARD(additive_operators, parsing_unary_operators);
    if (groups->integer_magnitudes.first)
        return create_error_from_token(memory->errors,
                                       groups->integer_magnitudes.first,
                                       CRITICAL,
                                       get_messages()->integer_literal_out_of_range);
    APPLY_BACKWARD(power_operators, parsing_power_operators);
    APPLY_FORWARD(multiplicative_operators, parsing_multiplicative_operators);
    APPLY_FORWARD(additive_operators, parsing_additive_operators);
    APPLY_FORWARD(shift_operators, parsing_logic_operators);
    APPLY_FORWARD(comparison_operators, parsing_comparison_operators);
    APPLY_FORWARD(equality_operators, parsing_comparison_operators);
    APPLY_FORWARD(bitwise_and_operators, parsing_logic_operators);
    APPLY_FORWARD(bitwise_xor_operators, parsing_logic_operators);
    APPLY_FORWARD(bitwise_or_operators, parsing_logic_operators);
    APPLY_FORWARD(logical_and_operators, parsing_logic_operators);
    APPLY_FORWARD(logical_or_operators, parsing_logic_operators);
    APPLY_FORWARD(inheritance_operators, parsing_inheritance);
    APPLY_BACKWARD(assignment_operators, parsing_assignment_operators);
    APPLY_FORWARD(function_arguments, parsing_function_call_args);
    APPLY_FORWARD(var_keywords, parsing_variable_declarations);
    APPLY_FORWARD(const_keywords, parsing_constant_declarations);
    APPLY_FORWARD(return_keywords, parsing_returns);
    APPLY_FORWARD(throw_keywords, parsing_throw);
    APPLY_FORWARD(loop_jumps, parsing_loop_jump);
    APPLY_BACKWARD(do_keywords, parsing_flow_keywords);
    APPLY_BACKWARD(control_flow_keywords, parsing_flow_keywords);
    APPLY_BACKWARD(else_keywords, parsing_else_keywords);
    APPLY_FORWARD(catch_keywords, parsing_unmatched_catch);
    APPLY_FORWARD(statement_lists, parsing_statement_list_bodies);
    APPLY_FORWARD(function_objects, parsing_function_bodies);
    APPLY_FORWARD(preprocessed_parenthesized_expressions, parsing_parenthesized_expressions);
    // add other rules...
    return error;
}

/** @brief Only a loop body grants jump targets; nested functions start a new domain. */
static compilation_error_t *
validate_loop_jumps(node_t *node, size_t depth, parser_memory_t *memory) {
    node_type_t type = node->vtbl->type;
    if (type == NODE_FUNCTION_OBJECT)
        depth = 0;
    if ((type == NODE_BREAK || type == NODE_CONTINUE) && !depth)
        return create_error_from_node(memory->errors,
                                      node,
                                      CRITICAL,
                                      get_messages()->loop_jump_outside);
    bool loop =
        type == NODE_FOR_IN || type == NODE_FOR || type == NODE_WHILE || type == NODE_DO_WHILE;
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        compilation_error_t *error = validate_loop_jumps(get_node_child(node, i),
                                                         loop ? (i == 3 ? depth + 1 : 0) : depth,
                                                         memory);
        if (error)
            return error;
    }
    return NULL;
}

compilation_error_t *
process_root_token_list(parser_memory_t *memory, token_list_t *tokens, node_t **root_node) {
    statement_list_processing_result_t stmt = process_statement_list(memory, tokens);
    if (stmt.error != NULL) {
        *root_node = NULL;
        return stmt.error;
    } else {
        *root_node = create_root_node(memory->graph, stmt.list);
        return validate_loop_jumps(*root_node, 0, memory);
    }
}
