/**
 * @file scanner.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Provides the implementation of the scanner functions for lexical analysis.
 */

#include "scanner.h"

#include "graph/expression.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "resources/messages.h"

#include <assert.h>
#include <math.h>
#include <memory.h>
#include <stdbool.h>
#include <stddef.h>
#include <wctype.h>

/** @brief The size of a tabulation (in columns). */
#define TABULATION_SIZE 4

/**
 * @brief Removes all comments from the given source code string.
 *
 * The function modifies the input string directly and does not allocate additional memory.
 */
static void remove_comments_and_carriage_returns(wchar_t *code) {
    int i = 0;
    while (code[i] != L'\0') {
        if (code[i] == L'\r') {
            code[i] = L' ';
            i++;
        } else if (code[i] == L'/' && code[i + 1] == L'/') {
            while (code[i] != L'\0' && code[i] != L'\n') {
                code[i] = L' ';
                i++;
            }
        } else if (code[i] == L'/' && code[i + 1] == L'*') {
            code[i] = L' ';
            code[i + 1] = L' ';
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

/** @brief Returns the current source character. */

static inline wchar_t get_char(scanner_t *scan) {
    return *scan->position.code;
}

/** @brief Returns the next character and updates the position of the scanner. */
static wchar_t next_char(scanner_t *scan) {
    wchar_t current = *scan->position.code;
    if (current == L'\n') {
        scan->position.row++;
        scan->position.column = 1;
    } else if (current == L'\t') {
        scan->position.column += TABULATION_SIZE;
    } else {
        scan->position.column++;
    }
    scan->position.offset++;
    return *(++scan->position.code);
}

/** @brief Accepts underscore and the identifier-letter ranges listed below. */
static bool is_letter(wchar_t c) {
    return (c >= L'A' && c <= L'Z') ||     // Uppercase Latin letters
           c == L'_' ||                    // Underscore is considered a letter in identifiers
           (c >= L'a' && c <= L'z') ||     // Lowercase Latin letters
           (c >= 0x0370 && c <= 0x03FF) || // Greek letters
           (c >= 0x0400 && c <= 0x04FF) || // Cyrillic letters
           (c >= 0x0530 && c <= 0x058F) || // Armenian letters
           (c >= 0x0590 && c <= 0x05FF) || // Hebrew letters
           (c >= 0x0600 && c <= 0x06FF) || // Arabic letters
           (c >= 0x0800 && c <= 0x083F) || // Syriac
           (c >= 0x0900 && c <= 0x097F) || // Devanagari (Hindi, Sanskrit, etc.)
           (c >= 0x0980 && c <= 0x09FF) || // Bengali
           (c >= 0x0A00 && c <= 0x0A7F) || // Gurmukhi
           (c >= 0x0A80 && c <= 0x0AFF) || // Gujarati
           (c >= 0x0B00 && c <= 0x0B7F) || // Oriya
           (c >= 0x0F00 && c <= 0x0FFF) || // Tibetan
           (c >= 0x1800 && c <= 0x18AF) || // Canadian Aboriginal syllabics
           (c >= 0x1D00 && c <= 0x1D7F) || // Phonetic Extensions
           (c >= 0x1E00 && c <= 0x1EFF) || // Latin Extended Additional
           (c >= 0x2C00 && c <= 0x2C5F) || // Glagolitic
           (c >= 0xA720 && c <= 0xA7FF) || // Latin Extended-D
           (c >= 0xA840 && c <= 0xA87F);   // Phags-pa
}

/**
 * @brief Checks if a wide character is considered an operator.
 * @return `true` if the character is an operator, `false` otherwise.
 */
static bool is_operator(wchar_t c) {
    const wchar_t *operators = L"+-*/%=!<>^&|~";
    return wcschr(operators, c) != NULL;
}

/** @brief Keyword to token type mapping */
typedef struct {
    const wchar_t *keyword;             /**< Keyword string */
    size_t length;                      /**< Length of keyword */
    token_type_t type;                  /**< Corresponding token type */
    node_t *(*node_factory)(arena_t *); /**< Optional AST node factory (NULL for simple keywords) */
    size_t group_offset;                /**< Optional group offset in the group structure */
} keyword_lookup_t;

/** @brief Keyword lookup table */
static const keyword_lookup_t keywords[] = {
    {L"var", 3, TOKEN_VAR, NULL, offsetof(token_groups_t, var_keywords)},
    {L"const", 5, TOKEN_CONST, NULL, offsetof(token_groups_t, const_keywords)},
    {L"null", 4, TOKEN_EXPRESSION, create_null_node, SIZE_MAX},
    {L"true", 4, TOKEN_EXPRESSION, create_true_node, SIZE_MAX},
    {L"false", 5, TOKEN_EXPRESSION, create_false_node, SIZE_MAX},
    {L"func", 4, TOKEN_FUNC, NULL, SIZE_MAX},
    {L"return", 6, TOKEN_RETURN, NULL, offsetof(token_groups_t, return_keywords)},
    {L"try", 3, TOKEN_TRY, NULL, offsetof(token_groups_t, control_flow_keywords)},
    {L"catch", 5, TOKEN_CATCH, NULL, offsetof(token_groups_t, catch_keywords)},
    {L"throw", 5, TOKEN_THROW, NULL, offsetof(token_groups_t, throw_keywords)},
    {L"for", 3, TOKEN_FOR, NULL, offsetof(token_groups_t, control_flow_keywords)},
    {L"if", 2, TOKEN_IF, NULL, offsetof(token_groups_t, control_flow_keywords)},
    {L"else", 4, TOKEN_ELSE, NULL, offsetof(token_groups_t, else_keywords)},

};

typedef struct {
    const wchar_t *oper; /**< Operator string */
    size_t group_offset; /**< Group offset in the group structure */
} operator_mapping_t;

static const operator_mapping_t operator_mappings[] = {
    {L"!", offsetof(token_groups_t, additive_operators)},
    {L"!!", offsetof(token_groups_t, additive_operators)},
    {L"~", offsetof(token_groups_t, additive_operators)},
    {L"&&", offsetof(token_groups_t, logical_and_operators)},
    {L"||", offsetof(token_groups_t, logical_or_operators)},
    {L"&", offsetof(token_groups_t, bitwise_and_operators)},
    {L"|", offsetof(token_groups_t, bitwise_or_operators)},
    {L"^", offsetof(token_groups_t, bitwise_xor_operators)},
    {L"<<", offsetof(token_groups_t, shift_operators)},
    {L">>", offsetof(token_groups_t, shift_operators)},
    {L"++", offsetof(token_groups_t, update_operators)},
    {L"--", offsetof(token_groups_t, update_operators)},
    {L"+", offsetof(token_groups_t, additive_operators)},
    {L"-", offsetof(token_groups_t, additive_operators)},
    {L"*", offsetof(token_groups_t, multiplicative_operators)},
    {L"/", offsetof(token_groups_t, multiplicative_operators)},
    {L"%", offsetof(token_groups_t, multiplicative_operators)},
    {L"**", offsetof(token_groups_t, power_operators)},
    {L"=", offsetof(token_groups_t, assignment_operators)},
    {L"==", offsetof(token_groups_t, equality_operators)},
    {L"!=", offsetof(token_groups_t, equality_operators)},
    {L"<=", offsetof(token_groups_t, comparison_operators)},
    {L">=", offsetof(token_groups_t, comparison_operators)},
    {L"<", offsetof(token_groups_t, comparison_operators)},
    {L">", offsetof(token_groups_t, comparison_operators)},

};

/**
 * @brief Parses a string literal in the source code.
 * `scan`: The scanner instance used for lexical analysis.
 */
static void parse_string(scanner_t *scan, token_t *token) {
    assert(get_char(scan) == L'"');
    token->type = TOKEN_EXPRESSION;
    wchar_t ch = next_char(scan);
    string_builder_t builder;
    init_string_builder(&builder, 0);
    while (ch != '"') {
        if (ch == L'\0') {
            token->type = TOKEN_ERROR;
            token->text.data = get_messages()->unclosed_quotation_mark;
            goto cleanup;
        }
        if (ch == L'\\') {
            ch = next_char(scan);
            switch (ch) {
                case L'\0':
                    token->type = TOKEN_ERROR;
                    token->text.data = get_messages()->unclosed_quotation_mark;
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
                    token->text = format_string_to_arena(scan->memory->tokens,
                                                         get_messages()->invalid_escape_sequence,
                                                         ch);
                    goto cleanup;
            }
        } else {
            append_char(&builder, ch);
        }
        ch = next_char(scan);
    }
    token->node = create_static_string_node(scan->memory->graph, builder.data, builder.length);
    next_char(scan);
cleanup:
    FREE(builder.data);
}

/**
 * @brief Parses a numeric literal (integer or real) in the source code.
 *
 * Parses a numeric literal starting at the current character, which must be a digit.
 * `scan`: The scanner instance used for lexical analysis.
 * @note Integer literals must fit the signed 64-bit range, including a leading minus.
 * @note The exponent part must follow the format `[eE][+/-]digits`.
 */
static void parse_number(scanner_t *scan, token_t *token) {
    wchar_t ch = get_char(scan);
    assert(iswdigit(ch));

    token->type = TOKEN_EXPRESSION;

    const wchar_t *begin = scan->position.code;
    uint64_t int_part = 0;
    uint64_t limit = (uint64_t)INT64_MAX + 1;
    bool overflow = false;
    while (iswdigit(ch)) {
        uint64_t digit = ch - '0';
        if (int_part > (limit - digit) / 10)
            overflow = true;
        else if (!overflow)
            int_part = int_part * 10 + digit;
        ch = next_char(scan);
    }

    bool is_real = false;
    if (ch == L'.') {
        is_real = true;
        ch = next_char(scan);
        while (iswdigit(ch))
            ch = next_char(scan);
    }
    if (ch == L'e' || ch == L'E') {
        is_real = true;
        ch = next_char(scan);
        if (ch == L'-' || ch == L'+')
            ch = next_char(scan);
        while (iswdigit(ch))
            ch = next_char(scan);
    }

    if (is_real) {
        /* Avoid overflowing integer accumulators for fractions and exponents. */
        double value = wcstod(begin, NULL);
        token->node = create_real_number_node(scan->memory->graph, value);
    } else {
        if (overflow) {
            token->type = TOKEN_ERROR;
            token->text.data = get_messages()->integer_literal_out_of_range;
            return;
        }
        if (int_part == (uint64_t)INT64_MAX + 1) {
            token->type = TOKEN_INTEGER_MAGNITUDE;
            append_token_to_group(&scan->groups->integer_magnitudes, token);
        } else {
            token->node = create_integer_node(scan->memory->graph, (int64_t)int_part);
        }
    }
}

scanner_t *create_scanner(const char *file_name,
                          string_value_t code,
                          parser_memory_t *memory,
                          token_groups_t *groups) {
    scanner_t *scan = alloc_zeroed_from_arena(memory->tokens, sizeof(scanner_t));
    size_t code_size = sizeof(wchar_t) * (code.length + 1);
    scan->code = alloc_from_arena(memory->tokens, code_size);
    memcpy(scan->code, code.data, code_size);
    remove_comments_and_carriage_returns(scan->code);
    scan->position = (full_position_t){file_name, 1, 1, scan->code, 0};
    scan->memory = memory;
    scan->groups = groups;
    memset(groups, 0, sizeof(token_groups_t));
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

    token_t *token = alloc_zeroed_from_arena(scan->memory->tokens, sizeof(token_t));

    full_position_t *begin = copy_full_position_to_arena(scan->memory->positions, &scan->position);

    if (is_letter(ch)) {
        bool predefined = false;
        do {
            ch = next_char(scan);
        } while (is_letter(ch) || iswdigit(ch));
        size_t length = scan->position.code - begin->code;
        for (size_t index = 0; index < sizeof(keywords) / sizeof(keyword_lookup_t); index++) {
            const keyword_lookup_t *kw = &keywords[index];
            if (length == kw->length && wcsncmp(begin->code, kw->keyword, kw->length) == 0) {
                predefined = true;
                token->type = kw->type;
                token->text = (string_view_t){kw->keyword, kw->length};
                if (kw->node_factory) {
                    token->node = kw->node_factory(scan->memory->graph);
                }
                if (kw->group_offset != SIZE_MAX) {
                    token_list_t *group =
                        (token_list_t *)((char *)(scan->groups) + kw->group_offset);
                    append_token_to_group(group, token);
                }
                break;
            }
        }
        if (!predefined) {
            token->type = TOKEN_IDENTIFIER;
            append_token_to_group(&scan->groups->identifiers, token);
        }
    } else if (is_operator(ch)) {
        token->type = TOKEN_OPERATOR;
        wchar_t first = ch, second = next_char(scan);
        for (size_t i = 0; i < sizeof(operator_mappings) / sizeof(*operator_mappings); i++) {
            const wchar_t *op = operator_mappings[i].oper;
            if (op[0] == first && op[1] && op[1] == second && op[2] == 0) {
                next_char(scan);
                break;
            }
        }
    } else if (ch == L'{' || ch == L'}' || ch == L'(' || ch == L')' || ch == L'[' || ch == L']') {
        token->type = TOKEN_BRACKET;
        next_char(scan);
    } else if (ch == L'"') {
        parse_string(scan, token);
    } else if (iswdigit(ch)) {
        parse_number(scan, token);
    } else if (ch == L',') {
        token->type = TOKEN_COMMA;
        token->text = (string_view_t){L",", 1};
        next_char(scan);
    } else if (ch == L';') {
        token->type = TOKEN_SEMICOLON;
        token->text = (string_view_t){L";", 1};
        next_char(scan);
    } else {
        token->type = TOKEN_ERROR;
        token->text =
            format_string_to_arena(scan->memory->tokens, get_messages()->unknown_symbol, ch);
        next_char(scan);
    }

    if (token->text.data == NULL) {
        size_t length = scan->position.code - begin->code;
        token->text = copy_string_to_arena(scan->memory->tokens, begin->code, length);
    } else if (token->text.length == 0) {
        token->text.length = wcslen(token->text.data);
    }

    short_position_t *end =
        create_short_position_from_full(scan->memory->positions, &scan->position);
    token->position = create_position_range(scan->memory->positions, begin, end);
    if (token->node) {
        token->node->position = token->position;
    }

    if (token->type == TOKEN_OPERATOR) {
        for (size_t index = 0; index < sizeof(operator_mappings) / sizeof(operator_mapping_t);
             index++) {
            if (wcscmp(operator_mappings[index].oper, token->text.data) == 0) {
                token_list_t *group = (token_list_t *)((char *)(scan->groups)
                                                       + operator_mappings[index].group_offset);
                append_token_to_group(group, token);
                break;
            }
        }
    }

    return token;
}
