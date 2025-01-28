/**
 * @file token.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines the structure for representing a token (lexeme) and its associated data.
 *
 * This file contains the definition of the `token_t` structure, which represents a token
 * (or lexeme) in the source code. It contains various fields for storing metadata about the
 * token, such as its type, position in the code, neighboring tokens in a doubly linked list,
 * and other attributes that describe the token's role in the lexical analysis phase.
 */

#pragma once

#include <stddef.h>
#include <wchar.h>

#include "position.h"

/**
 * @brief Forward declaration of token structure.
 */
typedef struct token_t token_t;

/**
 * @brief Forward declaration of list of tokens.
 */
typedef struct token_list_t token_list_t;

/**
 * @brief Forward declaration of AST node.
 */
typedef struct node_t node_t;

/**
 * @enum token_type_t
 * @brief Enum for different token types.
 * 
 * This enum represents the different types of tokens (lexemes) that can be encountered
 * during lexical analysis.
 */
typedef enum {
    TOKEN_IDENTIFIER,       /**< An identifier (variable, function name, etc.) */
    TOKEN_BRACKET,          /**< A bracket (e.g., '(', ')', '{', '}', '[', ']', etc.) */
    TOKEN_ERROR,            /**< An invalid token (error case) */

    TOKEN_BRACKET_PAIR,     /**< A pair of brackets and all tokens between them */
    TOKEN_EXPRESSION,       /**< An expression token, which contains an attached syntax tree node */
    TOKEN_STATEMENT,        /**< A statement (e.g., assignment, control structures, etc.) */
    // Other token types can be added here in the future
} token_type_t;

/**
 * @struct token_list_t
 * @brief Structure to represent a doubly linked list of tokens.
 *
 * This structure holds the list of tokens in a doubly linked list format. It maintains pointers
 * to the head and tail of the list, as well as a size field that tracks the number of tokens in
 * the list. This structure is used to organize tokens during the lexical analysis phase, and it
 * allows efficient traversal and modification of the list as tokens are processed.
 */
struct token_list_t {
    /**
     * @brief Pointer to the first token in the list.
     * 
     * This pointer points to the head (first) token in the doubly linked list. If the
     * list is empty, this pointer will be NULL.
     */
    token_t *first;

    /**
     * @brief Pointer to the last token in the list.
     * 
     * This pointer points to the tail (last) token in the doubly linked list. If the
     * list is empty, this pointer will be NULL.
     */
    token_t *last;

    /**
     * @brief The number of tokens in the list.
     * 
     * This field stores the total number of tokens currently in the list. It is used
     * to efficiently manage and track the size of the token list.
     */
    size_t count;
};

/**
 * @struct token_t
 * @brief Structure to represent a token (lexeme) in the source code.
 *
 * This structure holds the metadata for a token (or lexeme) in the source code. It includes:
 * - The type of the token.
 * - Its position in the source code (start and end).
 * - Links to other tokens in two separate doubly linked lists:
 *   1. **Neighbors list:** Tokens are linked in the order they appear in the source code.
 *      This preserves the original sequence of tokens for further analysis.
 *   2. **Group list:** Tokens are grouped by specific criteria, allowing for efficient
 *      selection and processing of tokens that share common characteristics.
 * - Additional metadata, such as the token's text, length, and a corresponding syntax tree node.
 */
struct token_t {
    /**
     * @brief The type of the token.
     *
     * This field stores the type of the token, such as whether it is an identifier, operator,
     * number, or static string.
     */
    token_type_t type;

    /**
     * @brief Pointer to the `token_list_t` representing the neighbors list.
     *
     * Tokens in this list are ordered exactly as they appear in the source code.
     */
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

    /**
     * @brief Pointer to the `token_list_t` representing the group list.
     *
     * Tokens in this list are grouped based on specific criteria. This allows for
     * efficient operations on all tokens within a group, such as applying a transformation
     * or processing a subset of tokens.
     */
    token_list_t *group;

    /**
     * @brief Pointer to the previous token in the group list.
     *
     * Links to the previous token in the group, based on the grouping criteria.
     */
    token_t *previous_in_group;

    /**
     * @brief Pointer to the next token in the group list.
     *
     * Links to the next token in the group, based on the grouping criteria.
     */
    token_t *next_in_group;

    /**
     * @brief The position where the token starts in the source code.
     *
     * Includes the file name, row, column, and offset.
     */
    full_position_t begin;

    /**
     * @brief The position of the character following the token in the source code.
     *
     * Includes the row and column of the character immediately after the token.
     */
    short_position_t end;

    /**
     * @brief The text of the token.
     *
     * A wide-character string representing the token's content, after necessary transformations
     * (e.g., unescaping strings or processing literals).
     */
    const wchar_t const *text;

    /**
     * @brief The length of the token's text.
     *
     * The number of characters (`wchar_t`) in the `text` field.
     */
    size_t length;

    /**
     * @brief Pointer to the corresponding node in the syntax tree.
     *
     * This field links the token to a node in the abstract syntax tree, if applicable.
     */
    node_t *node;

    /**
     * @brief The list of child tokens (if any).
     *
     * Tokens that are considered children of this token (e.g., tokens enclosed within
     * parentheses or part of a complex structure).
     */
    token_list_t children;
};

/**
 * @struct token_groups_t
 * @brief Structure to hold various groups of tokens.
 * 
 * This structure organizes tokens into different groups based on their types or roles.
 * Groups are represented as `token_list_t`, allowing efficient operations on subsets of tokens.
 */
typedef struct {
    /**
     * @brief Group for identifier tokens.
     */
    token_list_t identifiers;

    /**
     * @brief Group for additive operators ("plus" and "minus").
     */
    token_list_t additive_operators;
} token_groups_t;

/**
 * @brief Adds a token to the end of a neighbors list.
 * 
 * This function adds a token to the end of an existing neighbors list of tokens.
 * If the list is empty, the token becomes the head and tail of the list.
 *
 * @param neighbors The neighbors list to which the token should be added.
 * @param token The token to add to the neighbors list.
 */
void append_token_to_neighbors(token_list_t *neighbors, token_t *token);

/**
 * @brief Adds a token to the end of a group list.
 * 
 * This function adds a token to the end of an existing group list of tokens.
 * If the group is empty, the token becomes the head and tail of the group.
 *
 * @param group The group list to which the token should be added.
 * @param token The token to add to the group.
 */
void append_token_to_group(token_list_t *group, token_t *token);

/**
 * @brief Adds a token to the beginning of a neighbors list.
 * 
 * This function adds a token to the beginning of an existing neighbors list of tokens.
 * If the list is empty, the token becomes the head and tail of the list.
 *
 * @param neighbors The neighbors list to which the token should be added.
 * @param token The token to add to the neighbors list.
 */
void prepend_token_to_neighbors(token_list_t *neighbors, token_t *token);

/**
 * @brief Removes a token from its neighbors and group lists.
 * 
 * This function removes the specified token from its neighbors list and, if present,
 * also from its group list. The token is unlinked from both lists, and the neighboring
 * tokens' pointers are updated accordingly. The token itself is not freed, as memory
 * management is handled by arenas.
 *
 * @param token The token to remove.
 */
void remove_token(token_t *token);

/**
 * @brief Replaces a token in the neighbors list with another token.
 * 
 * This function replaces the specified token in the neighbors list with another token.
 * The old token is removed from the list and from its group, as it is considered processed 
 * (i.e., collapsed or transformed). The new token is inserted in its place, and the size 
 * of the neighbors list remains unchanged.
 * 
 * @param old_token The token to remove from the neighbors list and its group.
 * @param new_token The token to insert in place of the old token.
 */
void replace_token(token_t *old_token, token_t *new_token);
