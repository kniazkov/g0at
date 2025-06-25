/**
 * @file main.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Main entry point of the Goat interpreter.
 */

#include "lib/allocate.h"
#include "lib/io.h"
#include "resources/messages.h"
#include "cli/launcher.h"

/**
 * @brief Main entry point of the program.
 * 
 * This is the main entry point of the Goat interpreter. It initializes the message system,
 * parses command-line options, sets up input/output, and then launches the appropriate 
 * program components (compiler and/or virtual machine) using the `go` function.
 * 
 * @param argc The number of command-line arguments.
 * @param argv The array of command-line arguments.
 * 
 * @return An exit code:
 *         - `0` on success.
 *         - A non-zero value on failure (e.g., invalid options, initialization errors).
 */
int main(int argc, char** argv) {
    init_messages();

    options_t *opt = parse_options(argc, argv);
    if (opt == NULL) {
        return -1;
    }

    if (!init_io()) {
        // @todo message here
        return -1;
    }

    int ret_val = go(opt);

    destroy_options(opt);
    print_list_of_memory_blocks();

    return ret_val;
}
