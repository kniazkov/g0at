/** @file test_native_options.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Modes, defaults, repeated options and reports.
 */
#include "test_native_options.h"

#include "cli/options.h"
#include "test_macro.h"

#include <stdio.h>
#include <string.h>

bool test_native_options(void) {
    options_t *options = create_options();
    ASSERT(options->native_execution == NATIVE_OFF && !options->print_native
           && !options->native_output_file);
    destroy_options(options);
    char *modes[] = {"off", "auto", "required"};
    for (size_t i = 0; i < 3; i++) {
        char *args[] = {"goat",
                        "--native",
                        modes[i],
                        "--print-native",
                        "--save-native",
                        "first.txt",
                        "--save-native",
                        "second.txt",
                        "test.goat"};
        options = parse_options(9, args);
        ASSERT(options && options->native_execution == (native_execution_mode_t)i
               && options->print_native);
        ASSERT(!strcmp(options->native_output_file->file_name, "second.txt"));
        destroy_options(options);
    }
    char *args[] =
        {"goat", "--native", "required", "--native", "off", "--optimize", "none", "test.goat"};
    options = parse_options(8, args);
    ASSERT(options && options->native_execution == NATIVE_OFF
           && options->optimization_level == OPTIMIZATION_NONE);
    destroy_options(options);
    return true;
}
