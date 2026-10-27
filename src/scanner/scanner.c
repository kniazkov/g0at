/**
 * @file scanner.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Provides the implementation of the scanner functions for lexical analysis.
 */

#include "scanner.h"

#include "goat/unicode.h"
#include "graph/expression.h"
#include "lib/memory/allocate.h"
#include "lib/memory/arena.h"
#include "lib/strings/string_ext.h"
#include "resources/messages.h"

#include <assert.h>
#include <math.h>
#include <memory.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

/** @brief The size of a tabulation (in columns). */
#define TABULATION_SIZE 4

/**
 * @brief Removes all comments from the given source code string.
 *
 * The function modifies the input string directly and does not allocate additional memory.
 */
static void remove_comments_and_carriage_returns(gchar_t *code, size_t length) {
    size_t i = 0;
    while (i < length) {
        if (code[i] == U'"' || code[i] == U'\'') {
            gchar_t quote = code[i++];
            while (i < length) {
                if (code[i] == U'\\' && i + 1 < length) {
                    i += 2;
                    continue;
                }
                if (code[i++] == quote)
                    break;
            }
        } else if (code[i] == U'\r') {
            code[i++] = U' ';
        } else if (i + 1 < length && code[i] == U'/' && code[i + 1] == U'/') {
            while (i < length && code[i] != U'\n')
                code[i++] = U' ';
        } else if (i + 1 < length && code[i] == U'/' && code[i + 1] == U'*') {
            code[i++] = U' ';
            code[i++] = U' ';
            while (i < length) {
                if (i + 1 < length && code[i] == U'*' && code[i + 1] == U'/') {
                    code[i++] = U' ';
                    code[i++] = U' ';
                    break;
                }
                if (code[i] != U'\n')
                    code[i] = U' ';
                ++i;
            }
        } else
            ++i;
    }
}

/** @brief Outside the Unicode range, distinct from an embedded zero. */
#define END_OF_SOURCE UINT32_MAX

/** @brief Returns the current source character. */

static inline gchar_t get_char(scanner_t *scan) {
    return scan->position.offset < scan->length ? *scan->position.code : END_OF_SOURCE;
}

/** @brief Returns the next character and updates the position of the scanner. */
static gchar_t next_char(scanner_t *scan) {
    if (scan->position.offset >= scan->length)
        return END_OF_SOURCE;
    gchar_t current = *scan->position.code;
    if (current == U'\n') {
        scan->position.row++;
        scan->position.column = 1;
    } else if (current == U'\t') {
        scan->position.column += TABULATION_SIZE;
    } else {
        scan->position.column++;
    }
    scan->position.offset++;
    ++scan->position.code;
    return get_char(scan);
}

/** @brief Accepts underscore and the identifier-letter ranges listed below. */
static bool is_letter(gchar_t c) {
    return (c >= U'A' && c <= U'Z') ||     // Uppercase Latin letters
           c == U'_' ||                    // Underscore is considered a letter in identifiers
           (c >= U'a' && c <= U'z') ||     // Lowercase Latin letters
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
static bool is_operator(gchar_t c) {
    const gchar_t *operators = U"+-*/%=!<>^&|~.?";
    return c != 0 && STRCHR(operators, c) != NULL;
}

/** @brief Keyword to token type mapping */
typedef struct {
    const gchar_t *keyword;             /**< Keyword string */
    size_t length;                      /**< Length of keyword */
    token_type_t type;                  /**< Corresponding token type */
    node_t *(*node_factory)(arena_t *); /**< Optional AST node factory (NULL for simple keywords) */
    size_t group_offset;                /**< Optional group offset in the group structure */
} keyword_lookup_t;

/** @brief Keyword lookup table */
static const keyword_lookup_t keywords[] = {
    {U"var", 3, TOKEN_VAR, NULL, offsetof(token_groups_t, var_keywords)},
    {U"const", 5, TOKEN_CONST, NULL, offsetof(token_groups_t, const_keywords)},
    {U"public", 6, TOKEN_PUBLIC, NULL, offsetof(token_groups_t, access_keywords)},
    {U"external", 8, TOKEN_EXTERNAL, NULL, offsetof(token_groups_t, access_keywords)},
    {U"protected", 9, TOKEN_PROTECTED, NULL, offsetof(token_groups_t, access_keywords)},
    {U"private", 7, TOKEN_PRIVATE, NULL, offsetof(token_groups_t, access_keywords)},
    {U"null", 4, TOKEN_EXPRESSION, create_null_node, SIZE_MAX},
    {U"true", 4, TOKEN_EXPRESSION, create_true_node, SIZE_MAX},
    {U"false", 5, TOKEN_EXPRESSION, create_false_node, SIZE_MAX},
    {U"func", 4, TOKEN_FUNC, NULL, SIZE_MAX},
    {U"return", 6, TOKEN_RETURN, NULL, offsetof(token_groups_t, return_keywords)},
    {U"try", 3, TOKEN_TRY, NULL, offsetof(token_groups_t, control_flow_keywords)},
    {U"catch", 5, TOKEN_CATCH, NULL, offsetof(token_groups_t, catch_keywords)},
    {U"finally", 7, TOKEN_FINALLY, NULL, offsetof(token_groups_t, catch_keywords)},
    {U"throw", 5, TOKEN_THROW, NULL, offsetof(token_groups_t, throw_keywords)},
    {U"switch", 6, TOKEN_SWITCH, NULL, offsetof(token_groups_t, control_flow_keywords)},
    {U"case", 4, TOKEN_CASE, NULL, SIZE_MAX},
    {U"default", 7, TOKEN_DEFAULT, NULL, SIZE_MAX},
    {U"while", 5, TOKEN_WHILE, NULL, offsetof(token_groups_t, control_flow_keywords)},
    {U"do", 2, TOKEN_DO, NULL, offsetof(token_groups_t, do_keywords)},
    {U"break", 5, TOKEN_BREAK, NULL, offsetof(token_groups_t, loop_jumps)},
    {U"continue", 8, TOKEN_CONTINUE, NULL, offsetof(token_groups_t, loop_jumps)},
    {U"in", 2, TOKEN_IN, NULL, SIZE_MAX},
    {U"for", 3, TOKEN_FOR, NULL, offsetof(token_groups_t, control_flow_keywords)},
    {U"if", 2, TOKEN_IF, NULL, offsetof(token_groups_t, control_flow_keywords)},
    {U"else", 4, TOKEN_ELSE, NULL, offsetof(token_groups_t, else_keywords)},

};

typedef struct {
    const gchar_t *oper; /**< Operator string */
    size_t group_offset; /**< Group offset in the group structure */
} operator_mapping_t;

static const operator_mapping_t operator_mappings[] = {
    {U"!", offsetof(token_groups_t, additive_operators)},
    {U"!!", offsetof(token_groups_t, additive_operators)},
    {U"~", offsetof(token_groups_t, additive_operators)},
    {U"&&", offsetof(token_groups_t, logical_and_operators)},
    {U"||", offsetof(token_groups_t, logical_or_operators)},
    {U"&", offsetof(token_groups_t, bitwise_and_operators)},
    {U"|", offsetof(token_groups_t, bitwise_or_operators)},
    {U"^", offsetof(token_groups_t, bitwise_xor_operators)},
    {U"<<", offsetof(token_groups_t, shift_operators)},
    {U">>", offsetof(token_groups_t, shift_operators)},
    {U"++", offsetof(token_groups_t, update_operators)},
    {U"--", offsetof(token_groups_t, update_operators)},
    {U"+", offsetof(token_groups_t, additive_operators)},
    {U"->", offsetof(token_groups_t, inheritance_operators)},
    {U"-", offsetof(token_groups_t, additive_operators)},
    {U"*", offsetof(token_groups_t, multiplicative_operators)},
    {U"/", offsetof(token_groups_t, multiplicative_operators)},
    {U"%", offsetof(token_groups_t, multiplicative_operators)},
    {U"**", offsetof(token_groups_t, power_operators)},
    {U"+=", offsetof(token_groups_t, assignment_operators)},
    {U"-=", offsetof(token_groups_t, assignment_operators)},
    {U"*=", offsetof(token_groups_t, assignment_operators)},
    {U"**=", offsetof(token_groups_t, assignment_operators)},
    {U"/=", offsetof(token_groups_t, assignment_operators)},
    {U"%=", offsetof(token_groups_t, assignment_operators)},
    {U"&=", offsetof(token_groups_t, assignment_operators)},
    {U"|=", offsetof(token_groups_t, assignment_operators)},
    {U"^=", offsetof(token_groups_t, assignment_operators)},
    {U"<<=", offsetof(token_groups_t, assignment_operators)},
    {U">>=", offsetof(token_groups_t, assignment_operators)},
    {U"=", offsetof(token_groups_t, assignment_operators)},
    {U"?", offsetof(token_groups_t, assignment_operators)},
    {U"==", offsetof(token_groups_t, equality_operators)},
    {U"!=", offsetof(token_groups_t, equality_operators)},
    {U"<=", offsetof(token_groups_t, comparison_operators)},
    {U">=", offsetof(token_groups_t, comparison_operators)},
    {U"<", offsetof(token_groups_t, comparison_operators)},
    {U">", offsetof(token_groups_t, comparison_operators)},
    {U".", offsetof(token_groups_t, member_access_operators)},

};

/**
 * @brief Parses a string literal in the source code.
 * `scan`: The scanner instance used for lexical analysis.
 */
static void parse_string(scanner_t *scan, token_t *token) {
    assert(get_char(scan) == U'"');
    token->type = TOKEN_EXPRESSION;
    gchar_t ch = next_char(scan);
    string_builder_t builder;
    init_string_builder(&builder, 0);
    while (ch != '"') {
        if (ch == END_OF_SOURCE) {
            token->type = TOKEN_ERROR;
            token->text.data = get_messages()->unclosed_quotation_mark;
            goto cleanup;
        }
        if (ch == U'\\') {
            ch = next_char(scan);
            switch (ch) {
                case END_OF_SOURCE:
                    token->type = TOKEN_ERROR;
                    token->text.data = get_messages()->unclosed_quotation_mark;
                    goto cleanup;
                case U'0':
                    append_char(&builder, 0);
                    break;
                case U'r':
                    append_char(&builder, '\r');
                    break;
                case U'n':
                    append_char(&builder, '\n');
                    break;
                case U'b':
                    append_char(&builder, '\b');
                    break;
                case U't':
                    append_char(&builder, '\t');
                    break;
                case U'\\':
                case U'\'':
                case U'\"':
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
 * @brief Parses a character literal in the source code.
 *
 * Parses a single-quoted literal that must contain exactly one character (possibly an escape
 * sequence). `scan`: The scanner instance used for lexical analysis.
 */
static void parse_character(scanner_t *scan, token_t *token) {
    assert(get_char(scan) == U'\'');
    token->type = TOKEN_EXPRESSION;
    gchar_t ch = next_char(scan);
    uint32_t value;
    if (ch == U'\'') {
        next_char(scan);
        token->type = TOKEN_ERROR;
        token->text.data = get_messages()->empty_character_literal;
        return;
    }
    if (ch == END_OF_SOURCE) {
        token->type = TOKEN_ERROR;
        token->text.data = get_messages()->unclosed_quotation_mark;
        return;
    }
    if (ch == U'\\') {
        ch = next_char(scan);
        switch (ch) {
            case END_OF_SOURCE:
                token->type = TOKEN_ERROR;
                token->text.data = get_messages()->unclosed_quotation_mark;
                return;
            case U'0':
                value = 0;
                break;
            case U'U':
                value = 0;
                for (unsigned i = 0; i < 8; i++) {
                    ch = next_char(scan);
                    unsigned digit = ch >= U'0' && ch <= U'9'   ? (unsigned)(ch - U'0')
                                     : ch >= U'a' && ch <= U'f' ? (unsigned)(ch - U'a' + 10)
                                     : ch >= U'A' && ch <= U'F' ? (unsigned)(ch - U'A' + 10)
                                                                : 16;
                    if (digit == 16) {
                        token->type = TOKEN_ERROR;
                        token->text.data = get_messages()->invalid_character_literal;
                        while (ch != U'\'' && ch != END_OF_SOURCE)
                            ch = next_char(scan);
                        if (ch == U'\'')
                            next_char(scan);
                        return;
                    }
                    value = (value << 4) | digit;
                }
                break;
            case U'r':
                value = U'\r';
                break;
            case U'n':
                value = U'\n';
                break;
            case U'b':
                value = U'\b';
                break;
            case U't':
                value = U'\t';
                break;
            case U'\\':
                value = U'\\';
                break;
            case U'\'':
                value = U'\'';
                break;
            case U'"':
                value = U'"';
                break;
            default:
                token->type = TOKEN_ERROR;
                token->text = format_string_to_arena(scan->memory->tokens,
                                                     get_messages()->invalid_escape_sequence,
                                                     ch);
                while (ch != U'\'' && ch != END_OF_SOURCE)
                    ch = next_char(scan);
                if (ch == U'\'')
                    next_char(scan);
                return;
        }
    } else {
        value = (uint32_t)ch;
    }
    ch = next_char(scan);
    if (ch == END_OF_SOURCE) {
        token->type = TOKEN_ERROR;
        token->text.data = get_messages()->unclosed_quotation_mark;
        return;
    }
    if (ch != U'\'') {
        token->type = TOKEN_ERROR;
        token->text.data = get_messages()->invalid_character_literal;
        while (ch != U'\'' && ch != END_OF_SOURCE)
            ch = next_char(scan);
        if (ch == U'\'')
            next_char(scan);
        return;
    }
    next_char(scan);
    token->node = create_character_node(scan->memory->graph, value);
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
    gchar_t ch = get_char(scan);
    assert(ISDIGIT(ch));

    token->type = TOKEN_EXPRESSION;

    const gchar_t *begin = scan->position.code;
    uint64_t int_part = 0;
    uint64_t limit = (uint64_t)INT64_MAX + 1;
    bool overflow = false;
    while (ISDIGIT(ch)) {
        uint64_t digit = ch - '0';
        if (int_part > (limit - digit) / 10)
            overflow = true;
        else if (!overflow)
            int_part = int_part * 10 + digit;
        ch = next_char(scan);
    }

    bool is_real = false;
    if (ch == U'.') {
        is_real = true;
        ch = next_char(scan);
        while (ISDIGIT(ch))
            ch = next_char(scan);
    }
    if (ch == U'e' || ch == U'E') {
        is_real = true;
        ch = next_char(scan);
        if (ch == U'-' || ch == U'+')
            ch = next_char(scan);
        while (ISDIGIT(ch))
            ch = next_char(scan);
    }

    if (is_real) {
        /* Avoid overflowing integer accumulators for fractions and exponents. */
        double value = STRTOD(begin, NULL);
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
    if (code.length > SIZE_MAX / sizeof(gchar_t) - 1)
        abort();
    size_t code_size = sizeof(gchar_t) * (code.length + 1);
    scan->length = code.length;
    scan->code = alloc_from_arena(memory->tokens, code_size);
    memcpy(scan->code, code.data, code.length * sizeof(gchar_t));
    scan->code[code.length] = 0;
    remove_comments_and_carriage_returns(scan->code, code.length);
    scan->position = (full_position_t){file_name, 1, 1, scan->code, 0};
    scan->memory = memory;
    scan->groups = groups;
    memset(groups, 0, sizeof(token_groups_t));
    return scan;
}

token_t *get_token(scanner_t *scan) {
    gchar_t ch = get_char(scan);

    while (ISSPACE(ch)) {
        ch = next_char(scan);
    }

    if (ch == END_OF_SOURCE) {
        return NULL;
    }

    token_t *token = alloc_zeroed_from_arena(scan->memory->tokens, sizeof(token_t));

    full_position_t *begin = copy_full_position_to_arena(scan->memory->positions, &scan->position);

    if (is_letter(ch)) {
        bool predefined = false;
        do {
            ch = next_char(scan);
        } while (is_letter(ch) || ISDIGIT(ch));
        size_t length = scan->position.code - begin->code;
        for (size_t index = 0; index < sizeof(keywords) / sizeof(keyword_lookup_t); index++) {
            const keyword_lookup_t *kw = &keywords[index];
            if (length == kw->length && STRNCMP(begin->code, kw->keyword, kw->length) == 0) {
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
        size_t length = 1;
        for (size_t i = 0; i < sizeof(operator_mappings) / sizeof(*operator_mappings); i++) {
            const gchar_t *op = operator_mappings[i].oper;
            size_t n = STRLEN(op);
            if (n > length && !STRNCMP(scan->position.code, op, n))
                length = n;
        }
        for (size_t i = 0; i < length; i++)
            next_char(scan);
    } else if (ch == U'{' || ch == U'}' || ch == U'(' || ch == U')' || ch == U'[' || ch == U']') {
        token->type = TOKEN_BRACKET;
        next_char(scan);
    } else if (ch == U'"') {
        parse_string(scan, token);
    } else if (ch == U'\'') {
        parse_character(scan, token);
    } else if (ISDIGIT(ch)) {
        parse_number(scan, token);
    } else if (ch == U',') {
        token->type = TOKEN_COMMA;
        token->text = (string_view_t){U",", 1};
        next_char(scan);
    } else if (ch == U':') {
        token->type = TOKEN_COLON;
        next_char(scan);
    } else if (ch == U';') {
        token->type = TOKEN_SEMICOLON;
        token->text = (string_view_t){U";", 1};
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
        token->text.length = STRLEN(token->text.data);
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
            if (STRCMP(operator_mappings[index].oper, token->text.data) == 0) {
                token_list_t *group = (token_list_t *)((char *)(scan->groups)
                                                       + operator_mappings[index].group_offset);
                append_token_to_group(group, token);
                break;
            }
        }
    }

    return token;
}
