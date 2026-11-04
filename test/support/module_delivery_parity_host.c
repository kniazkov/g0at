/** @file module_delivery_parity_host.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Temporary owned-process supervisor for original/C delivery parity.
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/execution.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
#ifdef _WIN32
    if (!fs_arguments(&argc, &argv, NULL))
        return 1;
#endif
    int result = 1;
    if (argc >= 5) {
        char *end = NULL;
        errno = 0;
        double seconds = strtod(argv[3], &end);
        if (errno || end == argv[3] || *end || !isfinite(seconds) || seconds <= 0
            || seconds > 86400) {
            fputs("phase timeout must be finite, positive and at most 86400 seconds\n", stderr);
        } else {
            fs_error_t error = {0};
            char *cwd = absolute_fs_path(".", &error);
            if (cwd) {
                scenario_options_t options = {.name = "module-delivery-parity-phase",
                                              .build_directory = argv[1],
                                              .data_directory = argv[2],
                                              .keep_work = true};
                child_options_t child = {.executable = argv[4],
                                         .arguments = (const char *const *)(argv + 4),
                                         .working_directory = cwd,
                                         .empty_stdin = true};
                /* The shared execution helper saves complete streams, fails on
                 * cleanup errors, and owns descendants on every exit path. */
                uint64_t timeout_ms = (uint64_t)ceil(seconds * 1000.0);
                result = scenario_supervise(&options, timeout_ms, &child) ? 0 : 1;
            } else {
                fputs("cannot resolve parity phase working directory\n", stderr);
            }
            FREE(cwd);
            clear_fs_error(&error);
        }
    } else {
        fputs("usage: module_delivery_parity_host BUILD DATA TIMEOUT_SECONDS EXE [args...]\n",
              stderr);
    }
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
