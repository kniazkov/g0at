/**
 * @file analysis_test_support.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared parser setup for analysis tests.
 */
#include "analysis_test_support.h"

#include "lib/allocate.h"
#include "parser/parser.h"
#include "scanner/scanner.h"

node_t *parse_analysis_test_program(parser_memory_t *memory, string_value_t source) {
    token_groups_t *groups = CALLOC(sizeof(*groups));
    scanner_t *scan = create_scanner("test.goat", source, memory, groups);
    token_list_t tokens;
    parsing_result_t result = {0};
    node_t *root = NULL;
    compilation_error_t *error = process_brackets(memory, scan, &tokens, groups);
    if (!error) {
        error = apply_reduction_rules(groups, memory, &result);
    }
    if (!error) {
        error = process_root_token_list(memory, &tokens, &root);
    }
    FREE(groups);
    return error ? NULL : root;
}
