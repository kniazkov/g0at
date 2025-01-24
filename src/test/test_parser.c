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
    scanner_t *scan = create_scanner("program.goat", L"aaa ( bbb ccc ) ddd ", arena, NULL);
    token_list_t tokens;
    compilation_error_t *error = process_brackets(arena, scan, &tokens);
    ASSERT(error == NULL);
    ASSERT(tokens.size == 3);
    destroy_arena(arena);
    return true;
}
