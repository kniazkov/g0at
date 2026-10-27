/**
 * @file main.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Main entry point of the Goat interpreter.
 */

#include "cli/launcher.h"
#include "lib/files/file_system.h"
#include "lib/files/io.h"
#include "lib/memory/allocate.h"
#include "lib/process/cleanup_service.h"
#include "lib/strings/string_ext.h"
#include "resources/messages.h"

#include <stdlib.h>

/**
 * @brief Main entry point of the program.
 * @return An exit code: - `0` on success. - A non-zero value on failure (e.g., invalid options,
 * initialization errors).
 */
int main(int argc, char **argv) {
    int cleanup_code;
    if (cleanup_service_main(argc, argv, &cleanup_code))
        return cleanup_code;
    init_unicode_locale();
    init_messages();

    fs_error_t input_error = {0};
    if (!fs_arguments(&argc, &argv, &input_error)) {
        FPRINTF(stderr, "Command line: %s (%lu)\n", input_error.operation, input_error.native_code);
        clear_fs_error(&input_error);
        return EXIT_FAILURE;
    }
    options_t *opt = parse_options(argc, argv);
    if (opt == NULL) {
        free_fs_arguments(argc, argv);
        return EXIT_FAILURE;
    }

    if (!init_io()) {
        // @todo message here
        return EXIT_FAILURE;
    }

    int ret_val = go(opt);

    destroy_options(opt);
    free_fs_arguments(argc, argv);
    print_list_of_memory_blocks();

    /* Avoid Windows system() confusing a child exit code of -1 with launch failure. */
    return ret_val >= 0 && ret_val <= 255 ? ret_val : EXIT_FAILURE;
}
