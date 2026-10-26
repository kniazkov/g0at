/** @file inspect.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Nonexecuting module metadata inspection command.
 */
#include "cli/inspect.h"

#include "cli/inspect_report.h"
#include "lib/file_system.h"
#include "lib/string_ext.h"

#include <stdio.h>

int main(int argc, char **argv) {
    init_unicode_locale();
    fs_error_t error = {0};
    if (!fs_arguments(&argc, &argv, &error)) {
        bool written = true;
        if (inspect_wants_json(argc, argv))
            written = write_inspect_json(NULL,
                                         NULL,
                                         false,
                                         NULL,
                                         NULL,
                                         0,
                                         NULL,
                                         2,
                                         "INVALID_ARGUMENT",
                                         "Invalid Unicode command line.",
                                         NULL,
                                         NULL);
        else
            fputs("Invalid Unicode command line.\n", stderr);
        clear_fs_error(&error);
        return written ? 2 : 1;
    }
    int result = inspect_module_command(argc, argv);
    free_fs_arguments(argc, argv);
    return result;
}
