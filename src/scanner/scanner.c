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

#include <stdbool.h>

#include "scanner.h"
#include "lib/arena.h"
#include "lib/string_ext.h"

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
 * @brief Gets the current character from the scanner.
 * 
 * This function returns the current character the scanner is pointing to. It does not advance
 * the pointer or update the position in the source code.
 * 
 * @param scan The scanner instance from which to get the current character.
 * @return The current character being pointed to by the scanner.
 */
static inline wchar_t get_char(scanner_t *scan) {
    return *scan->ptr;
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
    wchar_t current = get_char(scan);
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
    return *scan->ptr++;
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

scanner_t *create_scanner(const char *file_name, wchar_t *code, arena_t *arena) {
    remove_comments_and_carriage_returns(code);
    scanner_t *scan = alloc_from_arena(arena, sizeof(scanner_t));
    scan->ptr = code;
    scan->position = (position_t){ file_name, 1, 1 };
    scan->arena = arena;
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

    token_t *token = alloc_zeroed_from_arena(scan->arena, sizeof(token_t));
    token->begin = scan->position;

    if (is_letter(ch)) {
        token->type = TOKEN_IDENTIFIER;


        while (iswalpha(ch) || iswdigit(ch) || ch == L'_') {
            ch = next_char(scan);
            token->length++;
        }

        return token;
    }

    return NULL; // here we add some
}
