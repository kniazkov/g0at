/**
 * @file token.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines the structure for representing a token (lexeme) and its associated data.
 */

#pragma once

#include "common/position.h"
#include "lib/value.h"

#include <stddef.h>
#include <wchar.h>

typedef struct token_t token_t;

typedef struct token_list_t token_list_t;

typedef struct node_t node_t;

/**
 * @brief Enum for different token types.
 *
 * This enum represents the different types of tokens (lexemes) that can be encountered during
 * lexical analysis.
 */
typedef enum {
    TOKEN_IDENTIFIER, /**< An identifier (variable, function name, etc.) */
    TOKEN_BRACKET,    /**< A bracket (e.g., '(', ')', '{', '}', '[', ']', etc.) */
    TOKEN_OPERATOR,   /**< An operator (e.g., '+', '-', '*', '/', '=', '==', etc.) */
    TOKEN_COMMA,      /**< Comma ',' used in parameter lists, array literals, etc. */
    TOKEN_SEMICOLON,  /**< Semicolon ';' used as statement terminator */
    TOKEN_ERROR,      /**< An invalid token (error case) */

    TOKEN_VAR,           /**< The 'var' keyword for variable declarations */
    TOKEN_CONST,         /**< The 'const' keyword for constant declarations */
    TOKEN_FUNC,          /**< The 'func' keyword for function declarations */
    TOKEN_RETURN,        /**< The 'return' keyword used in return statements */
    TOKEN_IF,            /**< The 'if' keyword used in if-else statements */
    TOKEN_TRY,           /**< The try keyword. */
    TOKEN_CATCH,         /**< The catch keyword. */
    TOKEN_THROW,         /**< The throw keyword. */
    TOKEN_CATCH_BINDING, /**< Validated catch identifier in parentheses. */
    TOKEN_ELSE,          /**< The 'else' keyword used in if-else statements */

    TOKEN_BRACKET_PAIR,           /**< A pair of brackets and all tokens between them */
    TOKEN_EXPRESSION,             /**< An expression token, which contains an attached
                                       syntax tree node */
    TOKEN_STATEMENT,              /**< A statement (e.g., assignment, control structures, etc.) */
    TOKEN_FCALL_ARGS,             /**< Unprocessed function call arguments */
    TOKEN_STATEMENT_LIST,         /**< Unprocessed statement list */
    TOKEN_EXPRESSION_IN_BRACKETS, /**< Unprocessed expression in brackets */
    TOKEN_FUNCTION_BODY,          /**< Unprocessed function body */
    // Other token types can be added here in the future
} token_type_t;

/**
 * @brief A doubly linked list of tokens.
 *
 * Is used to organize tokens during the lexical analysis phase, and it allows efficient traversal
 * and modification of the list as tokens are processed.
 */
struct token_list_t {
    /**
     * @brief Pointer to the first token in the list.
     *
     * If the list is empty, this pointer will be NULL.
     */
    token_t *first;

    /**
     * @brief Pointer to the last token in the list.
     *
     * If the list is empty, this pointer will be NULL.
     */
    token_t *last;

    /** @brief The number of tokens in the list. */
    size_t count;
};

/**
 * @brief Token in two independent lists: source neighbors and a parser reduction group.
 * Children hold bracket contents; node links a reduced token to its AST node.
 */
struct token_t {
    /** @brief The type of the token. */
    token_type_t type;

    /** @brief Pointer to the `token_list_t` representing the neighbors list. */
    token_list_t *neighbors;

    /**
     * @brief Pointer to the previous token in the neighbors list.
     *
     * Links to the token that appears immediately before this token in the source code.
     */
    token_t *left;

    /**
     * @brief Pointer to the next token in the neighbors list.
     *
     * Links to the token that appears immediately after this token in the source code.
     */
    token_t *right;

    /** @brief Pointer to the `token_list_t` representing the group list. */
    token_list_t *group;

    /** @brief Pointer to the previous token in the group list. */
    token_t *previous_in_group;

    /** @brief Pointer to the next token in the group list. */
    token_t *next_in_group;

    /**
     * @brief Pointer to the source range occupied by this token.
     *
     * It can be shared safely between different entities because the range data is allocated in an
     * independent memory arena.
     */
    position_range_t *position;

    /**
     * @brief The text of the token.
     *
     * A string representing the token's content, after necessary transformations (e.g., unescaping
     * strings or processing literals).
     */
    string_view_t text;

    /** @brief Pointer to the corresponding node in the syntax tree. */
    node_t *node;

    /** @brief The list of child tokens (if any). */
    token_list_t children;
};

/** @brief Structure to hold various groups of tokens. */
typedef struct {
    /** @brief Group for identifier tokens. */
    token_list_t identifiers;

    /** @brief Group for additive operators ("plus" and "minus"). */
    token_list_t additive_operators;

    token_list_t update_operators; /**< Prefix and postfix ++/--. */

    /** @brief Group for multiplicative operators ("multiply", "divide", and "modulus"). */
    token_list_t multiplicative_operators;

    /** @brief Group for power operators. */
    token_list_t power_operators;

    /** @brief Group for assignment operators. */
    token_list_t assignment_operators;

    /** @brief Group for comparison operators. */
    token_list_t comparison_operators;
    token_list_t equality_operators;

    /** @brief Unprocessed function call arguments. */
    token_list_t function_arguments;

    /** @brief Group for 'var' keyword tokens. */
    token_list_t var_keywords;

    /** @brief Group for 'const' keyword tokens. */
    token_list_t const_keywords;

    /** @brief Group for tokens within curly braces (scope blocks). */
    token_list_t scope_blocks;

    /** @brief Tokens containing statement lists whose bodies have not yet been processed. */
    token_list_t statement_lists;

    /** @brief Tokens containing expressions in parentheses that have not yet been processed at all.
     */
    token_list_t unprocessed_parenthesized_expressions;

    /** @brief Tokens containing expressions in parentheses that have passed the first part of
     * processing. */
    token_list_t preprocessed_parenthesized_expressions;

    /** @brief Tokens containing function objects whose bodies have not yet been processed. */
    token_list_t function_objects;

    /** @brief Group for 'return' keyword tokens. */
    token_list_t return_keywords;

    token_list_t throw_keywords; /**< Explicit throws. */
    token_list_t catch_keywords; /**< Catch headers and unmatched catch diagnostics. */

    /** @brief Group for control-flow keyword tokens. */
    token_list_t control_flow_keywords;

    /** @brief Group for 'else' keyword tokens. */
    token_list_t else_keywords;
} token_groups_t;

/** @brief Gets the textual representation of a token. */
string_value_t token_to_string(const token_t *token);

/** @brief Adds a token to the end of a neighbors list. */
void append_token_to_neighbors(token_list_t *neighbors, token_t *token);

/** @brief Adds a token to the end of a group list. */
void append_token_to_group(token_list_t *group, token_t *token);

/** @brief Adds a token to the beginning of a neighbors list. */
void prepend_token_to_neighbors(token_list_t *neighbors, token_t *token);

/**
 * @brief Removes a token from its group.
 *
 * If the group becomes empty after removal, the first and last pointers of the group are set to
 * NULL. The token is also unlinked from the group by setting its `group`, `previous_in_group`, and
 * `next_in_group` pointers to NULL.
 */
void remove_token_from_group(token_t *token);

/** @brief Removes a token from its neighbors and group lists. */
void remove_token(token_t *token);

/** @brief Replaces a token in the neighbors list with another token. */
void replace_token(token_t *old_token, token_t *new_token);
