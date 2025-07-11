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
    parser_memory_t memory = { arena, arena };
    token_groups_t groups;
    scanner_t *scan = create_scanner(
        "program.goat",
        STATIC_STRING(L"  test \n abc123  "),
        &memory,
        &groups
    );
    token_t *tok = get_token(scan);
    ASSERT(tok->type == TOKEN_IDENTIFIER);
    ASSERT(wcscmp(L"test", tok->text.data) == 0);
    ASSERT(tok->text.length == 4);
    ASSERT(strcmp("program.goat", tok->begin.file_name) == 0);
    ASSERT(tok->begin.row == 1);
    ASSERT(tok->begin.column == 3);
    ASSERT(tok->end.row == 1);
    ASSERT(tok->end.column == 7);
    tok = get_token(scan);
    ASSERT(tok->type == TOKEN_IDENTIFIER);
    ASSERT(wcscmp(L"abc123", tok->text.data) == 0);
    ASSERT(tok->text.length == 6);
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
    parser_memory_t memory = { arena, arena };
    token_groups_t groups;
    scanner_t *scan = create_scanner(
        "program.goat",
        STATIC_STRING(L"  )  "),
        &memory,
        &groups
    );
    token_t *tok = get_token(scan);
    ASSERT(tok->type == TOKEN_BRACKET);
    ASSERT(wcscmp(L")", tok->text.data) == 0);
    ASSERT(tok->text.length == 1);
    destroy_arena(arena);
    return true;
}

bool test_static_string() {
    arena_t *arena = create_arena();
    parser_memory_t memory = { arena, arena };
    token_groups_t groups;
    scanner_t *scan = create_scanner(
        "program.goat", 
        STATIC_STRING(L" \"test\" \"new\\nline\" \"\" \"not closed "),
        &memory,
        &groups
    );
    token_t *tok = get_token(scan);
    ASSERT(tok->type == TOKEN_EXPRESSION);
    ASSERT(wcscmp(L"\"test\"", tok->text.data) == 0);
    ASSERT(tok->text.length == 6);
    tok = get_token(scan);
    ASSERT(tok->type == TOKEN_EXPRESSION);
    ASSERT(wcscmp(L"\"new\\nline\"", tok->text.data) == 0);
    ASSERT(tok->text.length == 11);
    ASSERT(tok->node != NULL);
    ASSERT(tok->node->vtbl->type == NODE_STATIC_STRING);
    string_value_t value = tok->node->vtbl->generate_goat_code(tok->node);
    ASSERT(wcscmp(L"\"new\\nline\"", value.data) == 0);
    FREE_STRING(value);
    tok = get_token(scan);
    ASSERT(tok->type == TOKEN_EXPRESSION);
    ASSERT(wcscmp(L"\"\"", tok->text.data) == 0);
    ASSERT(tok->text.length == 2);
    tok = get_token(scan);
    ASSERT(tok->type == TOKEN_ERROR);
    ASSERT(tok->text.data != NULL && tok->text.length != 0);
    destroy_arena(arena);
    return true;
}

bool test_uknown_symbol() {
    arena_t *arena = create_arena();
    parser_memory_t memory = { arena, arena };
    token_groups_t groups;
    scanner_t *scan = create_scanner(
        "program.goat", 
        STATIC_STRING(L"  `  "), 
        &memory, 
        &groups
    );
    token_t *tok = get_token(scan);
    ASSERT(tok->type == TOKEN_ERROR);
    ASSERT(wcscmp(L"Unknown symbol '`'", tok->text.data) == 0);
    destroy_arena(arena);
    return true;
}
