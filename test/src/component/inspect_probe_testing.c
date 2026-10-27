/** @file inspect_probe_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Inspector host with isolated test-only child and janitor fault injection.
 */
#include "cli/inspect.h"
#include "cli/inspect_probe.h"
#include "lib/files/file_system.h"
#include "lib/process/process_channel.h"
#include "lib/strings/string_ext.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include "lib/platform/windows_target.h"

#    include <windows.h>
#else
#    include <unistd.h>
#endif

int main(int argc, char **argv) {
    init_unicode_locale();
    fs_error_t error = {0};
    if (!fs_arguments(&argc, &argv, &error))
        return 90;
    int result = 0;
    if (argc == 5 && !strcmp(argv[1], CLEANUP_SERVICE_ARGUMENT)) {
        const char *marker = getenv("GOAT_INSPECT_CLEANER_PID");
        if (marker) {
            FILE *file = fopen(marker, "wb");
#ifdef _WIN32
            unsigned long pid = GetCurrentProcessId();
#else
            unsigned long pid = (unsigned long)getpid();
#endif
            if (!file || fprintf(file, "%lu\n", pid) < 0 || fclose(file))
                return 91;
        }
        const char *fault = getenv("GOAT_INSPECT_CLEANER_FAULT");
        if (fault && !strcmp(fault, "exit"))
            return 3;
        if (fault && !strcmp(fault, "silent")) {
            child_wait_parent((uintptr_t)strtoull(argv[2], NULL, 10), 0);
            return 0;
        }
    }
    if (!cleanup_service_main(argc, argv, &result) && !inspect_probe_main(argc, argv, &result))
        result = inspect_module_command(argc, argv);
    free_fs_arguments(argc, argv);
    clear_fs_error(&error);
    return result;
}
