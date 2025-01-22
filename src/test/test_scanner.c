/**
 * @file test_scanner.c
 * @copyright 2025 Ivan Kniazkov
 * @brief A set of tests for testing scanner.
 */

#include <stdio.h>
#include <string.h>

#include "test_scanner.h"
#include "test_macro.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "scanner/scanner.h"
#include "graph/node.h"

bool test_identifier() {
    arena_t *arena = create_arena();
    scanner_t *scan = create_scanner("program.goat", L"  test \n abc123  ", arena, NULL);
    token_t *tok = get_token(scan);
    ASSERT(tok->type == TOKEN_IDENTIFIER);
    ASSERT(wcscmp(L"test", tok->text) == 0);
    ASSERT(tok->length == 4);
    ASSERT(strcmp("program.goat", tok->begin.file_name) == 0);
    ASSERT(tok->begin.row == 1);
    ASSERT(tok->begin.column == 3);
    ASSERT(tok->end.row == 1);
    ASSERT(tok->end.column == 7);
    tok = get_token(scan);
    ASSERT(tok->type == TOKEN_IDENTIFIER);
    ASSERT(wcscmp(L"abc123", tok->text) == 0);
    ASSERT(tok->length == 6);
    ASSERT(tok->begin.row == 2);
    ASSERT(tok->begin.column == 2);
    ASSERT(tok->end.row == 2);
    ASSERT(tok->end.column == 8);
    tok = get_token(scan);
    ASSERT(tok == NULL);
    destroy_arena(arena);
    return true;
}

bool test_bracket() {
    arena_t *arena = create_arena();
    scanner_t *scan = create_scanner("program.goat", L"  )  ", arena, NULL);
    token_t *tok = get_token(scan);
    ASSERT(tok->type == TOKEN_BRACKET);
    ASSERT(wcscmp(L")", tok->text) == 0);
    ASSERT(tok->length == 1);
    destroy_arena(arena);
    return true;
}

bool test_static_string() {
    arena_t *tokens = create_arena();
    arena_t *graph = create_arena();
    scanner_t *scan = create_scanner("program.goat", 
        L" \"test\" \"new\\nline\" \"\" \"not closed ",
        tokens, graph);
    token_t *tok = get_token(scan);
    ASSERT(tok->type == TOKEN_STRING);
    ASSERT(wcscmp(L"test", tok->text) == 0);
    ASSERT(tok->length == 4);
    tok = get_token(scan);
    ASSERT(tok->type == TOKEN_STRING);
    ASSERT(wcscmp(L"new\nline", tok->text) == 0);
    ASSERT(tok->length == 8);
    ASSERT(tok->node != NULL);
    ASSERT(tok->node->vtbl->type == NODE_STATIC_STRING);
    string_value_t value = tok->node->vtbl->to_string(tok->node);
    ASSERT(wcscmp(L"\"new\\nline\"", value.data) == 0);
    if (value.should_free) {
        FREE(value.data);
    }
    tok = get_token(scan);
    ASSERT(tok->type == TOKEN_STRING);
    ASSERT(wcscmp(L"", tok->text) == 0);
    ASSERT(tok->length == 0);
    tok = get_token(scan);
    ASSERT(tok->type == TOKEN_ERROR);
    ASSERT(tok->text != NULL && tok->length != 0);
    destroy_arena(tokens);
    destroy_arena(graph);
    return true;
}

bool test_uknown_symbol() {
    arena_t *arena = create_arena();
    scanner_t *scan = create_scanner("program.goat", L"  `  ", arena, NULL);
    token_t *tok = get_token(scan);
    ASSERT(tok->type == TOKEN_ERROR);
    ASSERT(wcscmp(L"unknown symbol '`'", tok->text) == 0);
    destroy_arena(arena);
    return true;
}
