/**
 * @file test_analysis_options.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Analysis report command-line options.
 */
#include <stdio.h>
#include <string.h>
#include "test_analysis.h"
#include "test_macro.h"
#include "cli/options.h"

bool test_analysis_options() {
    char *args[] = { "goat", "--print-analysis", "--save-analysis", "first.txt",
        "--save-analysis", "second.txt", "--save-graph", "ast.svg", "test.goat" };
    options_t *options = parse_options(9, args);
    ASSERT(options && options->print_analysis && options->analysis_output_file);
    ASSERT(strcmp(options->analysis_output_file->file_name, "second.txt") == 0);
    ASSERT(options->graph_output_file);
    ASSERT(strcmp(options->graph_output_file->file_name, "ast.svg") == 0);
    destroy_options(options);
    options = create_options();
    ASSERT(!options->print_analysis && !options->analysis_output_file);
    destroy_options(options);
    return true;
}

