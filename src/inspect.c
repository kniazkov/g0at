/** @file inspect.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Nonexecuting module metadata inspection command.
 */
#include "cli/inspect.h"

#include "lib/file_system.h"
#include "lib/string_ext.h"

#include <stdio.h>

int main(int argc, char **argv) {
    init_unicode_locale();
    fs_error_t error = {0};
    if (!fs_arguments(&argc, &argv, &error)) {
        fputs("Invalid Unicode command line.\n", stderr);
        clear_fs_error(&error);
        return 2;
    }
    int result = inspect_module_command(argc, argv);
    free_fs_arguments(argc, argv);
    return result;
}
