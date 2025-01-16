/**
 * @file test_scanner.c
 * @copyright 2025 Ivan Kniazkov
 * @brief A set of tests for testing scanner.
 */

#include <stdio.h>

#include "test_scanner.h"
#include "test_macro.h"
#include "lib/arena.h"
#include "scanner/scanner.h"

bool test_identifier() {
    arena_t *arena = create_arena();
    scanner_t *scan = create_scanner("program.goat", L"  test  ", arena);
    token_t *tok = get_token(scan);
    ASSERT(tok->type == TOKEN_IDENTIFIER);
    ASSERT(wcscmp(L"test", tok->text) == 0);
    ASSERT(tok->length == 4);
    destroy_arena(arena);
    return true;
}
