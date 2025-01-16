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
 * @brief Forward declaration of AST node.
 */
typedef struct node_t node_t;

/**
 * @enum token_type_t
 * @brief Enum for different token types.
 * 
 * This enum represents the different types of tokens (lexemes) that can be encountered
 * during lexical analysis. A token can be an identifier, bracket, static string, or other
 * types that may be defined as the language evolves.
 */
typedef enum {
    TOKEN_IDENTIFIER,         /**< An identifier (variable, function name, etc.) */
    TOKEN_NUMBER,             /**< A numeric value (integer, floating-point, etc.) */
    TOKEN_OPERATOR,           /**< An operator (e.g., '+', '-', '*', '/', '=', etc.) */
    TOKEN_KEYWORD,            /**< A keyword (e.g., 'if', 'else', 'while', etc.) */
    TOKEN_STRING,             /**< A string literal (e.g., "Hello, World!") */
    TOKEN_CHAR,               /**< A character literal (e.g., 'a', '1', etc.) */
    TOKEN_BRACKET,            /**< A bracket (e.g., '(', ')', '{', '}', '[', ']', etc.) */
    TOKEN_STATIC_STRING,      /**< A static string (e.g., a literal string in the source code) */
    TOKEN_ERROR,              /**< An invalid token (error case) */
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
typedef struct {
    /**
     * @brief Pointer to the first token in the list.
     * 
     * This pointer points to the head (first) token in the doubly linked list. If the
     * list is empty, this pointer will be NULL.
     */
    token_t *head;

    /**
     * @brief Pointer to the last token in the list.
     * 
     * This pointer points to the tail (last) token in the doubly linked list. If the
     * list is empty, this pointer will be NULL.
     */
    token_t *tail;

    /**
     * @brief The number of tokens in the list.
     * 
     * This field stores the total number of tokens currently in the list. It is used
     * to efficiently manage and track the size of the token list.
     */
    size_t size;
} token_list_t;

/**
 * @struct token_t
 * @brief Structure to represent a token (lexeme) in the source code.
 * 
 * This structure holds the metadata for a token (or lexeme) in the source code, including
 * its type, position (begin and end), neighboring tokens in a doubly linked list, and additional
 * attributes related to the token's role in the lexical analysis or syntax tree.
 */
struct token_t {
    /**
     * @brief The type of the token.
     * 
     * This field stores the type of the token, such as whether it is an identifier, operator,
     * number, or static string. The token type helps determine the token's role in the source code
     * and how it should be processed.
     */
    token_type_t type;

    /**
     * @brief Pointer to the token's list in the doubly linked list.
     * 
     * This pointer refers to the `token_list_t` that contains this token. It helps in the
     * organization of tokens in a linked list structure.
     */
    token_list_t *list;

    /**
     * @brief Pointer to the previous token in the list.
     * 
     * This pointer links to the previous token in the doubly linked list, allowing traversal
     * of the list in reverse order.
     */
    token_t *previous;

    /**
     * @brief Pointer to the next token in the list.
     * 
     * This pointer links to the next token in the doubly linked list, allowing traversal
     * of the list in forward order.
     */
    token_t *next;

    /**
     * @brief The position where the token starts in the source code.
     * 
     * This field stores the starting position of the token in the source code, represented
     * as a `position_t` structure that contains the file name, row, and column of the beginning
     * of the token.
     */
    position_t begin;

    /**
     * @brief The position of the character following the token in the source code.
     * 
     * This field stores the position immediately **after** the token in the source code,
     * represented as a `position_t` structure. It includes the file name, row, and column
     * of the character **following** the last character of the token.
     * 
     * This is commonly used to mark the boundary of the token, indicating where the
     * token ends and the next part of the code begins.
     */
    position_t end;

    /**
     * @brief The text of the token.
     * 
     * This field contains a wide-character string representing the token's text.
     * The text is independent from the source code, meaning it can be generated, copied,
     * or statically defined. For example, in the case of a `STATIC_STRING` token, the quotes and
     * escape sequences (like `\n`, `\t`, etc.) are removed and replaced with corresponding
     * entities.
     */
    wchar_t *text;

    /**
     * @brief The length of the token's text.
     * 
     * This field stores the number of characters (`wchar_t`) in the `text` field, which represents
     * the token's actual content.
     */
    size_t length;

    /**
     * @brief Pointer to the corresponding node in the syntax tree.
     * 
     * This field points to the `node_t` structure that represents the corresponding syntax tree
     * node for the token, if applicable. This allows the token to be associated with its
     * corresponding node in the abstract syntax tree.
     */
    node_t *node;

    /**
     * @brief The list of child tokens (if any).
     * 
     * This field stores a list of tokens that are considered children of the current token.
     * This is used for tokens that are part of more complex structures (e.g., parentheses
     * around expressions or nested blocks).
     */
    token_list_t child_tokens;
};


/**
 * @brief Adds a token to the end of a token list.
 * 
 * This function adds a token to the end of an existing doubly linked list of tokens.
 * If the list is empty, the token becomes the head and tail of the list.
 *
 * @param list The token list to which the token should be added.
 * @param token The token to add to the list.
 */
void append_token_to_list(token_list_t *list, token_t *token);

/**
 * @brief Adds a token to the beginning of a token list.
 * 
 * This function adds a token to the beginning of an existing doubly linked list of tokens.
 * If the list is empty, the token becomes the head and tail of the list.
 *
 * @param list The token list to which the token should be added.
 * @param token The token to add to the list.
 */
void prepend_token_to_list(token_list_t *list, token_t *token);

/**
 * @brief Removes a token from its list.
 * 
 * This function removes the specified token [from its list]. The token is unlinked from the list,
 * and the neighboring tokens' pointers are updated accordingly.
 *
 * @param token The token to remove.
 */
void remove_token(token_t *token);

/**
 * @brief Replaces a token in its list with another token.
 * 
 * This function replaces the specified token in the list with another token.
 * The old token is removed from the list, and the new token is inserted in its place.
 * The size of the list remains unchanged.
 *
 * @param old_token The token to remove from the list.
 * @param new_token The token to insert in place of the old token.
 */
void replace_token(token_t *old_token, token_t *new_token);
