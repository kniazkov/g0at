/** @file byte_literal_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Byte literal parsing, canonical round trips and backend admission.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "analysis/analysis.h"
#include "cli/options.h"
#include "graph/node.h"
#include "lib/memory/allocate.h"
#include "lib/memory/arena.h"
#include "scanner/scanner.h"
#include "unit/analysis_test_support.h"

#include <assert.h>

int main(void) {
    size_t before = get_allocated_memory_size();
    arena_t *arena = create_arena(256);
    parser_memory_t memory = {arena, arena, arena, arena};
    const gchar_t *valid[] = {U"#", U"#00", U"#aAbB00fF", U"#AA;", U"#AA[0]", U"#AA/* comment */;"};
    const gchar_t *expected[] = {U"#", U"#00", U"#AABB00FF", U"#AA", U"#AA", U"#AA"};
    for (size_t i = 0; i < 6; ++i) {
        token_groups_t groups;
        scanner_t *scan = create_scanner("bytes.goat",
                                         (string_value_t){valid[i], STRLEN(valid[i]), false},
                                         &memory,
                                         &groups);
        token_t *token = get_token(scan);
        assert(token->type == TOKEN_EXPRESSION && token->node->vtbl->type == NODE_BYTE_ARRAY);
        assert(token->position->begin->column == 1);
        string_value_t text = token->node->vtbl->generate_goat_code(token->node);
        assert(!STRCMP(text.data, expected[i]));
        scanner_t *again = create_scanner("round.goat", text, &memory, &groups);
        token_t *round = get_token(again);
        assert(round->node->vtbl->type == NODE_BYTE_ARRAY);
        string_value_t text2 = round->node->vtbl->generate_goat_code(round->node);
        assert(text.length == text2.length && !STRCMP(text.data, text2.data));
        FREE_STRING(text);
        FREE_STRING(text2);
    }
    const gchar_t *invalid[] = {U"#0", U"#ABC", U"#AABBGG", U"#AAя", U"#AA_", U"#AA#BB"};
    for (size_t i = 0; i < 6; ++i) {
        token_groups_t groups;
        scanner_t *scan = create_scanner("bad.goat",
                                         (string_value_t){invalid[i], STRLEN(invalid[i]), false},
                                         &memory,
                                         &groups);
        token_t *token = get_token(scan);
        assert(token->type == TOKEN_ERROR
               && token->position->end->column == STRLEN(invalid[i]) + 1);
        assert(!get_token(scan));
    }
    node_t *root =
        parse_analysis_test_program(&memory,
                                    STATIC_STRING(U"var ByteArray = 1; var x = #00ff; x[0];"));
    assert(root);
    options_t options = {0};
    compilation_error_t *error = analyze(root, &memory, &options, NULL);
    assert(error && error->severity == ERROR && error->position);
    root = parse_analysis_test_program(&memory, STATIC_STRING(U"if (false) { var x = #; }"));
    assert(root && analyze(root, &memory, &options, NULL));
    destroy_arena(arena);
    assert(get_allocated_memory_size() == before);
    return 0;
}
