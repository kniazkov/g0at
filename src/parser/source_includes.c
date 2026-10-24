/** @file source_includes.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Lexical discovery of literal include and import directives.
 */
#include "source_includes.h"

#include "graph/node.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "scanner/scanner.h"

#include <stdint.h>
#include <string.h>

void clear_source_includes(source_includes_t *includes) {
    if (!includes)
        return;
    for (size_t i = 0; i < includes->count; ++i)
        FREE(includes->items[i].path);
    FREE(includes->items);
    *includes = (source_includes_t){0};
}

/** @brief Saves a stable diagnostic before scanner arenas are released. */
static void fail_at(source_include_error_t *error, const token_t *token, const char *message) {
    if (!error)
        return;
    const full_position_t *p = token ? token->position->begin : NULL;
    *error =
        (source_include_error_t){p ? p->row : 1, p ? p->column : 1, p ? p->offset : 0, message};
}

/** @brief Exact identifier matching; comments and strings are already distinct tokens. */
static bool directive_keyword(const token_t *token, bool imports) {
    const gchar_t *keyword = imports ? U"import" : U"include";
    size_t length = imports ? 6 : 7;
    return token->type == TOKEN_IDENTIFIER && token->text.length == length
           && !MEMCMP(token->text.data, keyword, length);
}

/** @brief Grows a checked vector without mixing allocator families. */
static bool grow(void **data, size_t *capacity, size_t item_size) {
    size_t next = *capacity ? *capacity * 2 : 16;
    if (next < *capacity || next > SIZE_MAX / item_size)
        return false;
    void *fresh = ALLOC(next * item_size);
    if (*capacity)
        memcpy(fresh, *data, *capacity * item_size);
    FREE(*data);
    *data = fresh;
    *capacity = next;
    return true;
}

static bool parse_source_directives(const char *filename,
                                    const char *utf8,
                                    size_t length,
                                    source_includes_t *out,
                                    source_include_error_t *error,
                                    bool imports) {
    const char *required = imports ? "import requires a string literal module specification"
                                   : "include requires a string literal path";
    const char *empty = imports ? "import specification must be nonempty and contain no NUL"
                                : "include path must be nonempty and contain no NUL";
    const char *expression =
        imports ? "import specification must be one string literal, not an expression"
                : "include path must be one string literal, not an expression";
    if (error)
        *error = (source_include_error_t){0};
    if (!out || (!utf8 && length)) {
        fail_at(error, NULL, "invalid source input");
        return false;
    }
    *out = (source_includes_t){0};
    string_value_t code = decode_utf8_n(utf8 ? utf8 : "", length);
    if (!code.data) {
        fail_at(error, NULL, "source is not valid UTF-8");
        return false;
    }
    parser_memory_t memory = {create_arena(4), create_arena(8), create_arena(4), create_arena(1)};
    token_groups_t groups = {0};
    scanner_t *scan = create_scanner(filename ? filename : "<source>", code, &memory, &groups);
    FREE_STRING(code);
    source_includes_t result = {0};
    size_t capacity = 0, bracket_count = 0, bracket_capacity = 0;
    token_t **brackets = NULL;
    token_t *pending = NULL, *literal = NULL, *previous = NULL;
    bool ok = false;
    token_t *token;
    while ((token = get_token(scan)) != NULL) {
        if (token->type == TOKEN_ERROR) {
            fail_at(error, token, "invalid source token");
            goto done;
        }
        if (pending) {
            if (!token->node || token->node->vtbl->type != NODE_STATIC_STRING) {
                fail_at(error, pending, required);
                goto done;
            }
            node_display_value_t value = token->node->vtbl->get_data(token->node);
            char *path = encode_utf8_path(VALUE_TO_VIEW(value.text));
            FREE_STRING(value.text);
            if (!path || !*path) {
                FREE(path);
                fail_at(error, pending, empty);
                goto done;
            }
            if (result.count == capacity
                && !grow((void **)&result.items, &capacity, sizeof(*result.items))) {
                FREE(path);
                fail_at(error,
                        pending,
                        imports ? "too many import occurrences" : "too many include occurrences");
                goto done;
            }
            const full_position_t *p = pending->position->begin;
            result.items[result.count++] = (source_include_t){path, p->row, p->column, p->offset};
            pending = NULL;
            literal = token;
            previous = token;
            continue;
        }
        if (literal) {
            if ((token->type == TOKEN_OPERATOR && token->text.length == 1
                 && token->text.data[0] == U'+')
                || (token->node && token->node->vtbl->type == NODE_STATIC_STRING)
                || (token->type == TOKEN_BRACKET && token->text.data[0] == U'(')) {
                fail_at(error, literal, expression);
                goto done;
            }
            literal = NULL;
        }
        if (directive_keyword(token, imports)
            && !(previous && previous->type == TOKEN_OPERATOR && previous->text.length == 1
                 && previous->text.data[0] == U'.')) {
            pending = token;
        }
        if (token->type == TOKEN_BRACKET) {
            gchar_t ch = token->text.data[0];
            if (ch == U'(' || ch == U'[' || ch == U'{') {
                if (bracket_count == bracket_capacity
                    && !grow((void **)&brackets, &bracket_capacity, sizeof(*brackets))) {
                    fail_at(error, token, "too many source brackets");
                    goto done;
                }
                brackets[bracket_count++] = token;
            } else {
                gchar_t expected = ch == U')' ? U'(' : ch == U']' ? U'[' : U'{';
                if (!bracket_count || brackets[bracket_count - 1]->text.data[0] != expected) {
                    fail_at(error, token, "unmatched source bracket");
                    goto done;
                }
                --bracket_count;
            }
        }
        previous = token;
    }
    if (pending) {
        fail_at(error, pending, required);
        goto done;
    }
    if (bracket_count) {
        fail_at(error, brackets[bracket_count - 1], "unclosed source bracket");
        goto done;
    }
    *out = result;
    result = (source_includes_t){0};
    ok = true;
done:
    clear_source_includes(&result);
    FREE(brackets);
    destroy_arena(memory.positions);
    destroy_arena(memory.tokens);
    destroy_arena(memory.graph);
    destroy_arena(memory.errors);
    return ok;
}

bool parse_source_includes(const char *filename,
                           const char *utf8,
                           size_t length,
                           source_includes_t *out,
                           source_include_error_t *error) {
    return parse_source_directives(filename, utf8, length, out, error, false);
}

bool parse_source_imports(const char *filename,
                          const char *utf8,
                          size_t length,
                          source_includes_t *out,
                          source_include_error_t *error) {
    return parse_source_directives(filename, utf8, length, out, error, true);
}
