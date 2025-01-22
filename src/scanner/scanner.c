/**
 * @file scanner.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Provides the implementation of the scanner functions for lexical analysis.
 *
 * This file contains the implementation of the scanner functions, which handle
 * the process of lexical analysis by reading characters from the source code, extracting tokens,
 * and updating the scanner's position in the source code.

 * The scanner uses an arena-based memory allocation scheme for efficient token management,
 * where memory is allocated in chunks and freed in bulk when the scanner is cleared.
 * This approach avoids the overhead of frequent small allocations and deallocations during
 * the lexical analysis phase, making it well-suited for performance-critical applications.
 */

#include <assert.h>
#include <memory.h>
#include <stdbool.h>
#include <wctype.h>

#include "scanner.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "resources/messages.h"
#include "graph/expression.h"

/**
 * @brief The size of a tabulation (in columns).
 * 
 * This constant defines the width of a tab character (`\t`). By default, it is set to 4,
 * but it can be adjusted if a different tab width is required. The tabulation width is used
 * in the `next_char` function to correctly update the column number when a tab character
 * is encountered.
 */
#define TABULATION_SIZE 4

/**
 * @brief Removes all comments from the given source code string.
 * 
 * This function processes the input string `code` and removes all comments (both single-line 
 * and multi-line), as well as all carriage return characters (`\r`), replacing them with spaces.
 * The resulting string will have the same structure as the input code, but without any comments 
 * or `\r` characters, while preserving the rest of the code.
 * 
 * The function modifies the input string directly and does not allocate additional memory.
 * 
 * @param code A wide-character string (`wchar_t *`) representing the source code.
 *  The input string will be modified in place, and all comments will be replaced with spaces.
 */
static void remove_comments_and_carriage_returns(wchar_t *code) {
    int i = 0;
    while (code[i] != L'\0') {
        if (code[i] == L'\r') {
            code[i] = L' ';
            i++;
        }
        else if (code[i] == L'/' && code[i + 1] == L'/') {
            while (code[i] != L'\0' && code[i] != L'\n') {
                code[i] = L' ';
                i++;
            }
        }
        else if (code[i] == L'/' && code[i + 1] == L'*') {
            i += 2;
            while (code[i] != L'\0' && !(code[i] == L'*' && code[i + 1] == L'/')) {
                code[i] = L' ';
                i++;
            }
            if (code[i] == L'*' && code[i + 1] == L'/') {
                code[i] = L' ';
                code[i + 1] = L' ';
                i += 2;
            }
        } else {
            i++;
        }
    }
}

/**
 * @brief Retrieves the current character from the scanner's position.
 *
 * This inline function returns the character currently pointed to by the scanner
 * without advancing the position. It is useful for inspecting the current character
 * in the source code during lexical analysis.
 * 
 * @param scan The scanner instance that tracks the current position in the source code.
 * @return The current character in the source code.
 */

static inline wchar_t get_char(scanner_t *scan) {
    return *scan->position.code;
}

/**
 * @brief Returns the next character and updates the position of the scanner.
 * 
 * This function advances the scanner to the next character in the source code.
 * It updates the position based on the current character:
 * - If the current character is a newline (`\n`), the line number is incremented and the column
 *   is reset to 1.
 * - If the current character is a tab (`\t`), the column is incremented by `TABULATION_SIZE`.
 * - Otherwise, the column is incremented by 1.
 * 
 * @param scan The scanner instance that tracks the current position and character.
 * @return The next character in the source code.
 */
static wchar_t next_char(scanner_t *scan) {
    wchar_t current = *scan->position.code;
    if (current == L'\n') {
        scan->position.row++;
        scan->position.column = 1;
    }
    else if (current == L'\t') {
        scan->position.column += TABULATION_SIZE;
    }
    else {
        scan->position.column++;
    }
    scan->position.offset++;
    return *(++scan->position.code);
}

/**
 * @brief Checks if a wide character is considered a letter.
 * 
 * This function checks whether the given wide character `c` is considered a letter.
 * It supports letters from multiple alphabets, including:
 * - Latin (A-Z, a-z)
 * - Greek (U+0370 to U+03FF)
 * - Cyrillic (U+0400 to U+04FF)
 * - Armenian (U+0530 to U+058F)
 * - Hebrew (U+0590 to U+05FF)
 * - Arabic (U+0600 to U+06FF)
 * - Indic scripts (U+0900 to U+097F, U+0980 to U+09FF, etc.)
 * - Various other alphabets in the Unicode standard.
 * 
 * The function also includes `_` as a valid letter, which is commonly used in
 * programming languages for variable names and identifiers.
 * 
 * @param c The character to check.
 * @return `true` if the character is a letter, `false` otherwise.
 */
static bool is_letter(wchar_t c) {
    return
        (c >= L'A' && c <= L'Z') ||      // Uppercase Latin letters
        c == L'_' ||                     // Underscore is considered a letter in identifiers
        (c >= L'a' && c <= L'z') ||      // Lowercase Latin letters
        (c >= 0x0370 && c <= 0x03FF) ||  // Greek letters
        (c >= 0x0400 && c <= 0x04FF) ||  // Cyrillic letters
        (c >= 0x0530 && c <= 0x058F) ||  // Armenian letters
        (c >= 0x0590 && c <= 0x05FF) ||  // Hebrew letters
        (c >= 0x0600 && c <= 0x06FF) ||  // Arabic letters
        (c >= 0x0800 && c <= 0x083F) ||  // Syriac
        (c >= 0x0900 && c <= 0x097F) ||  // Devanagari (Hindi, Sanskrit, etc.)
        (c >= 0x0980 && c <= 0x09FF) ||  // Bengali
        (c >= 0x0A00 && c <= 0x0A7F) ||  // Gurmukhi
        (c >= 0x0A80 && c <= 0x0AFF) ||  // Gujarati
        (c >= 0x0B00 && c <= 0x0B7F) ||  // Oriya
        (c >= 0x0F00 && c <= 0x0FFF) ||  // Tibetan
        (c >= 0x1800 && c <= 0x18AF) ||  // Canadian Aboriginal syllabics
        (c >= 0x1D00 && c <= 0x1D7F) ||  // Phonetic Extensions
        (c >= 0x1E00 && c <= 0x1EFF) ||  // Latin Extended Additional
        (c >= 0x2C00 && c <= 0x2C5F) ||  // Glagolitic
        (c >= 0xA720 && c <= 0xA7FF) ||  // Latin Extended-D
        (c >= 0xA840 && c <= 0xA87F);    // Phags-pa
}

/**
 * @brief Parses a string literal in the source code.
 *
 * This function parses a string literal starting with a double quote (`"`) and handles
 * escape sequences inside the string. It updates the provided token with the parsed
 * string or sets it as an error if the string is malformed.
 *
 * @param scan The scanner instance used for lexical analysis.
 * @param token The token to store the parsed string or error message.
 */
static void parse_string(scanner_t *scan, token_t *token) {
    assert(get_char(scan) == L'"');
    wchar_t ch = next_char(scan);
    string_builder_t builder;
    init_string_builder(&builder, 0);
    while (ch != '"') {
        if (ch == L'\0') {
            token->type = TOKEN_ERROR;
            token->text = get_messages()->unclosed_quotation_mark;
            goto cleanup;
        }
        if (ch == L'\\') {
            ch = next_char(scan);
            switch(ch) {
                case L'\0':
                    token->type = TOKEN_ERROR;
                    token->text = get_messages()->unclosed_quotation_mark;
                    goto cleanup;
                case L'r':
                    append_char(&builder, '\r');
                    break;
                case L'n':
                    append_char(&builder, '\n');
                    break;
                case L'b':
                    append_char(&builder, '\b');
                    break;
                case L't':
                    append_char(&builder, '\t');
                    break;
                case L'\\':
                case L'\'':
                case L'\"':
                    append_char(&builder, ch);
                    break;
                default:
                    token->type = TOKEN_ERROR;
                    token->text = format_string_to_arena(scan->tokens_memory, &token->length,
                        get_messages()->invalid_escape_sequence, ch);
                    goto cleanup;
            }
        } else {
            append_char(&builder, ch);
        }
        ch = next_char(scan);
    }
    token->type = TOKEN_STRING;
    if (builder.length > 0) {
        size_t data_size = (builder.length + 1) * sizeof(wchar_t);
        wchar_t *buffer = alloc_from_arena(scan->tokens_memory, data_size);
        memcpy(buffer, builder.data, data_size);
        token->text = buffer;
        token->length = builder.length;
        token->node = create_static_string_node(scan->graph_memory, builder.data, builder.length);
    } else {
        token->text = L"";
    }
    next_char(scan);
cleanup:
    FREE(builder.data);
}

scanner_t *create_scanner(const char *file_name, wchar_t *code, arena_t *tokens_memory,
        arena_t *graph_memory) {
    remove_comments_and_carriage_returns(code);
    scanner_t *scan = alloc_from_arena(tokens_memory, sizeof(scanner_t));
    scan->position = (full_position_t){ file_name, 1, 1, code, 0 };
    scan->tokens_memory = tokens_memory;
    scan->graph_memory = graph_memory;
    return scan;
}

token_t *get_token(scanner_t *scan) {
    wchar_t ch = get_char(scan);

    while (iswspace(ch)) {
        ch = next_char(scan);
    }

    if (ch == L'\0') {
        return NULL;
    }

    token_t *token = alloc_zeroed_from_arena(scan->tokens_memory, sizeof(token_t));
    token->begin = scan->position;

    if (is_letter(ch)) {
        token->type = TOKEN_IDENTIFIER;
        do {
            ch = next_char(scan);
        } while(is_letter(ch) || iswdigit(ch));
    }
    else if (ch == L'{' || ch == L'}' || ch == L'(' || ch == L')' || ch == L'[' || ch == L']') {
        token->type = TOKEN_BRACKET;
        next_char(scan);
    }
    else if (ch == L'"') {
        parse_string(scan, token);
    }
    else {
        token->type = TOKEN_ERROR;
        token->text = format_string_to_arena(scan->tokens_memory, &token->length,
            get_messages()->unknown_symbol, ch);
        next_char(scan);
    }
    
    token->end = full_to_short_position(&scan->position);
    if (token->text == NULL) {
        size_t length = scan->position.code - token->begin.code;
        wchar_t *text = alloc_from_arena(scan->tokens_memory, sizeof(wchar_t) * (length + 1));
        memcpy(text, token->begin.code, sizeof(wchar_t) * length);
        text[length] = L'\0';
        token->text = text;
        token->length = length;
    } else if (token->length == 0) {
        token->length = wcslen(token->text);
    }

    return token;
}
