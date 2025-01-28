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
#include "graph/node.h"

bool test_brackets_one_level_nesting() {
    arena_t *arena = create_arena();
    parser_memory_t memory = { arena, arena };
    token_groups_t groups;
    scanner_t *scan = create_scanner("program.goat", L"aaa ( \"bbb\" ccc ) ddd ", &memory, &groups);
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
    parser_memory_t memory = { arena, arena };
    token_groups_t groups;
    scanner_t *scan = create_scanner("program.goat", L"aaa ( \"bbb\" [ ccc ddd ] ) eee ",
        &memory, &groups);
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
    ASSERT(groups.identifiers.count == 4);
    destroy_arena(arena);
    return true;
}

bool test_unclosed_bracket() {
    arena_t *arena = create_arena();
    parser_memory_t memory = { arena, arena };
    token_groups_t groups;
    scanner_t *scan = create_scanner("program.goat", L"aaa ( bbb", &memory, &groups);
    token_list_t tokens;
    compilation_error_t *error = process_brackets(arena, scan, &tokens);
    ASSERT(error != NULL);
    ASSERT(strcmp("program.goat", error->begin.file_name) == 0);
    ASSERT(error->begin.row == 1);
    ASSERT(error->begin.column == 5);
    ASSERT(error->begin.code[0] == L'(');
    ASSERT(error->end.row == 1);
    ASSERT(error->end.column == 10);
    ASSERT(wcscmp(L"unclosed opening bracket: expected a closing bracket to match '('",
        error->message) == 0);
    destroy_arena(arena);
    return true;
}

bool test_missing_opening_bracket() {
    arena_t *arena = create_arena();
    parser_memory_t memory = { arena, arena };
    token_groups_t groups;
    scanner_t *scan = create_scanner("program.goat", L"aaa \n bbb ] ccc", &memory, &groups);
    token_list_t tokens;
    compilation_error_t *error = process_brackets(arena, scan, &tokens);
    ASSERT(error != NULL);
    ASSERT(error->begin.row == 2);
    ASSERT(error->begin.column == 6);
    ASSERT(error->begin.code[0] == L']');
    ASSERT(error->end.row == 2);
    ASSERT(error->end.column == 7);
    ASSERT(wcscmp(L"missing opening bracket corresponding to ']'", error->message) == 0);
    destroy_arena(arena);
    return true;
}

bool test_closing_bracket_does_not_match_opening() {
    arena_t *arena = create_arena();
    parser_memory_t memory = { arena, arena };
    token_groups_t groups;
    scanner_t *scan = create_scanner("program.goat", L"aaa { bbb \n ccc ] ddd",  &memory, &groups);
    token_list_t tokens;
    compilation_error_t *error = process_brackets(arena, scan, &tokens);
    ASSERT(error != NULL);
    ASSERT(error->begin.row == 1);
    ASSERT(error->begin.column == 5);
    ASSERT(error->begin.code[0] == L'{');
    ASSERT(error->end.row == 2);
    ASSERT(error->end.column == 7);
    ASSERT(wcscmp(L"closing bracket ']' does not match the opening bracket '{'",
        error->message) == 0);
    destroy_arena(arena);
    return true;
}

bool test_parsing_function_calls() {
    arena_t *arena = create_arena();
    parser_memory_t memory = { arena, arena };
    token_groups_t groups;
    wchar_t *code = L"print(\"test\")";
    scanner_t *scan = create_scanner("program.goat", code, &memory, &groups);
    token_list_t tokens;
    compilation_error_t *error = process_brackets(arena, scan, &tokens);
    ASSERT(error == NULL);
    ASSERT(tokens.count == 2);
    ASSERT(groups.identifiers.count == 1);
    error = apply_reduction_rules(&groups, &memory);
    ASSERT(error == NULL);
    ASSERT(tokens.count == 1);
    ASSERT(groups.identifiers.count == 0);
    ASSERT(tokens.first->node->vtbl->type == NODE_FUNCTION_CALL);
    string_value_t code2 = tokens.first->node->vtbl->to_string(tokens.first->node);
    ASSERT(wcscmp(code, code2.data) == 0);
    if (code2.should_free) {
        FREE(code2.data);
    }
    destroy_arena(arena);
    return true;
}
