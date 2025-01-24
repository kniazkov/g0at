/**
 * @file test_scanner.c
 * @copyright 2025 Ivan Kniazkov
 * @brief A set of tests for testing parser.
 */

#include <stdio.h>
#include <string.h>

#include "test_scanner.h"
#include "test_macro.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "scanner/scanner.h"
#include "parser/parser.h"

bool test_brackets_one_level_nesting() {
    arena_t *arena = create_arena();
    scanner_t *scan = create_scanner("program.goat", L"aaa ( \"bbb\" ccc ) ddd ", arena, arena);
    token_list_t tokens;
    compilation_error_t *error = process_brackets(arena, scan, &tokens);
    ASSERT(error == NULL);
    ASSERT(tokens.count == 3);
    token_t *token = tokens.first;
    ASSERT(token->type == TOKEN_IDENTIFIER);
    ASSERT(token->right->type == TOKEN_BRACKET_PAIR);
    ASSERT(token->right->right->type == TOKEN_IDENTIFIER);
    ASSERT(wcscmp(token->right->text, L"()") == 0);
    ASSERT(token->right->children.count == 2);
    ASSERT(token->right->children.first->type == TOKEN_EXPRESSION);
    ASSERT(token->right->children.first->right->type == TOKEN_IDENTIFIER);
    destroy_arena(arena);
    return true;
}

bool test_brackets_two_levels_nesting() {
    arena_t *arena = create_arena();
    scanner_t *scan = create_scanner("program.goat", L"aaa ( \"bbb\" [ ccc ddd ] ) eee ",
        arena, arena);
    token_list_t tokens;
    compilation_error_t *error = process_brackets(arena, scan, &tokens);
    ASSERT(error == NULL);
    ASSERT(tokens.count == 3);
    token_t *token = tokens.first;
    ASSERT(token->type == TOKEN_IDENTIFIER);
    ASSERT(token->right->type == TOKEN_BRACKET_PAIR);
    ASSERT(token->right->right->type == TOKEN_IDENTIFIER);
    ASSERT(wcscmp(token->right->text, L"()") == 0);
    ASSERT(token->right->children.count == 2);
    ASSERT(token->right->children.first->type == TOKEN_EXPRESSION);
    ASSERT(token->right->children.first->right->type == TOKEN_BRACKET_PAIR);
    ASSERT(wcscmp(token->right->children.first->right->text, L"[]") == 0);
    ASSERT(token->right->children.first->right->children.count == 2);
    ASSERT(scan->groups.identifiers.count == 4);
    destroy_arena(arena);
    return true;
}
