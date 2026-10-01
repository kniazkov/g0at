/**
 * @file main.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Main entry point of the Goat interpreter.
 */

#include <stdlib.h>

#include "lib/allocate.h"
#include "lib/io.h"
#include "resources/messages.h"
#include "cli/launcher.h"

/**
 * @brief Main entry point of the program.
 * @return An exit code: - `0` on success. - A non-zero value on failure (e.g., invalid options,
 * initialization errors).
 */
int main(int argc, char** argv) {
    init_messages();

    options_t *opt = parse_options(argc, argv);
    if (opt == NULL) {
        return EXIT_FAILURE;
    }

    if (!init_io()) {
        // @todo message here
        return EXIT_FAILURE;
    }

    int ret_val = go(opt);

    destroy_options(opt);
    print_list_of_memory_blocks();

    /* Avoid Windows system() confusing a child exit code of -1 with launch failure. */
    return ret_val == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
