/**
 * @file test_scanner.c
 * @copyright 2026 Ivan Kniazkov
 * @brief A set of tests for testing scanner.
 */

#include "test_scanner.h"

#include "graph/node.h"
#include "lib/memory/allocate.h"
#include "lib/memory/arena.h"
#include "scanner/scanner.h"
#include "test_macro.h"

#include <stdio.h>
#include <string.h>

bool test_identifier() {
    arena_t *arena = create_arena(8);
    parser_memory_t memory = {arena, arena, arena, arena};
    token_groups_t groups;
    scanner_t *scan =
        create_scanner("program.goat", STATIC_STRING(U"  test \n abc123  "), &memory, &groups);
    token_t *tok = get_token(scan);
    ASSERT(tok->type == TOKEN_IDENTIFIER);
    ASSERT(STRCMP(U"test", tok->text.data) == 0);
    ASSERT(tok->text.length == 4);
    ASSERT(strcmp("program.goat", tok->position->begin->file_name) == 0);
    ASSERT(tok->position->begin->row == 1);
    ASSERT(tok->position->begin->column == 3);
    ASSERT(tok->position->end->row == 1);
    ASSERT(tok->position->end->column == 7);
    tok = get_token(scan);
    ASSERT(tok->type == TOKEN_IDENTIFIER);
    ASSERT(STRCMP(U"abc123", tok->text.data) == 0);
    ASSERT(tok->text.length == 6);
    ASSERT(tok->position->begin->row == 2);
    ASSERT(tok->position->begin->column == 2);
    ASSERT(tok->position->end->row == 2);
    ASSERT(tok->position->end->column == 8);
    tok = get_token(scan);
    ASSERT(tok == NULL);
    destroy_arena(arena);
    return true;
}

bool test_bracket() {
    arena_t *arena = create_arena(8);
    parser_memory_t memory = {arena, arena, arena, arena};
    token_groups_t groups;
    scanner_t *scan = create_scanner("program.goat", STATIC_STRING(U"  )  "), &memory, &groups);
    token_t *tok = get_token(scan);
    ASSERT(tok->type == TOKEN_BRACKET);
    ASSERT(STRCMP(U")", tok->text.data) == 0);
    ASSERT(tok->text.length == 1);
    destroy_arena(arena);
    return true;
}

bool test_static_string() {
    arena_t *arena = create_arena(8);
    parser_memory_t memory = {arena, arena, arena, arena};
    token_groups_t groups;
    scanner_t *scan = create_scanner("program.goat",
                                     STATIC_STRING(U" \"test\" \"new\\nline\" \"\" \"not closed "),
                                     &memory,
                                     &groups);
    token_t *tok = get_token(scan);
    ASSERT(tok->type == TOKEN_EXPRESSION);
    ASSERT(STRCMP(U"\"test\"", tok->text.data) == 0);
    ASSERT(tok->text.length == 6);
    tok = get_token(scan);
    ASSERT(tok->type == TOKEN_EXPRESSION);
    ASSERT(STRCMP(U"\"new\\nline\"", tok->text.data) == 0);
    ASSERT(tok->text.length == 11);
    ASSERT(tok->node != NULL);
    ASSERT(tok->node->vtbl->type == NODE_STATIC_STRING);
    string_value_t value = generate_goat_code_from_node(tok->node);
    ASSERT(STRCMP(U"\"new\\nline\"", value.data) == 0);
    FREE_STRING(value);
    tok = get_token(scan);
    ASSERT(tok->type == TOKEN_EXPRESSION);
    ASSERT(STRCMP(U"\"\"", tok->text.data) == 0);
    ASSERT(tok->text.length == 2);
    tok = get_token(scan);
    ASSERT(tok->type == TOKEN_ERROR);
    ASSERT(tok->text.data != NULL && tok->text.length != 0);
    destroy_arena(arena);
    return true;
}

bool test_uknown_symbol() {
    arena_t *arena = create_arena(8);
    parser_memory_t memory = {arena, arena, arena, arena};
    token_groups_t groups;
    scanner_t *scan = create_scanner("program.goat", STATIC_STRING(U"  `  "), &memory, &groups);
    token_t *tok = get_token(scan);
    ASSERT(tok->type == TOKEN_ERROR);
    ASSERT(STRCMP(U"Unknown symbol '`'", tok->text.data) == 0);
    destroy_arena(arena);
    return true;
}
