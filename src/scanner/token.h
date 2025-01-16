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
    TOKEN_IDENTIFIER, /**< An identifier (variable, function name, etc.) */
    TOKEN_BRACKET,    /**< A bracket (e.g., '(', ')', '{', '}', etc.) */
    TOKEN_STATIC_STRING, /**< A static string (e.g., a literal string in the source code) */
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
 * its type, position, neighboring tokens in a doubly linked list, and additional attributes
 * related to the token's role in the lexical analysis or syntax tree.
 */
struct token_t {
    /**
     * @brief The type of the token.
     * 
     * This field stores the type of the token, such as whether it is an identifier, bracket,
     * or static string. The token type helps determine the token's role in the source code.
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
    struct token_t *previous;

    /**
     * @brief Pointer to the next token in the list.
     * 
     * This pointer links to the next token in the doubly linked list, allowing traversal
     * of the list in forward order.
     */
    struct token_t *next;

    /**
     * @brief Pointer to the position of the token in the source code.
     * 
     * This field stores the position of the token (file, row, column) within the source code.
     * It helps for error reporting and debugging.
     */
    const struct position_t *position;

    /**
     * @brief Pointer to the first character of the token in the source code.
     * 
     * This pointer points to the beginning of the token's text in the source code, which
     * is represented as a wide-character string.
     */
    wchar_t *text;

    /**
     * @brief The length of the token in characters.
     * 
     * This field stores the number of characters (wchar_t) that the token occupies in the
     * source code.
     */
    size_t length;

    /**
     * @brief Pointer to the corresponding node in the syntax tree.
     * 
     * This field points to the `node_t` structure that represents the corresponding syntax
     * tree node for the token, if applicable.
     */
    node_t *node;

    /**
     * @brief The list of child tokens (if any).
     * 
     * This field stores a list of tokens that are considered children of the current token.
     * This is used for tokens that are part of more complex structures (e.g., parentheses
     * around expressions).
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
