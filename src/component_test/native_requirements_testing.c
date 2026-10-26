/** @file native_requirements_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Native image inspection harness; never loads a library.
 */
#include "codegen/native_requirements.h"
#include "lib/allocate.h"
#include "lib/file_system.h"

#include <stdio.h>
#include <string.h>

static char *resolve(const char *name, void *context) {
    return join_fs_path(context, name, NULL);
}

int main(int argc, char **argv) {
    if (!fs_arguments(&argc, &argv, NULL))
        return 2;
    if (argc != 4) {
        free_fs_arguments(argc, argv);
        return 2;
    }
    goat_native_environment expected = {.os = GOAT_OS_LINUX,
                                        .architecture = GOAT_ARCH_X86_64,
                                        .bits = 64,
                                        .little_endian = true,
                                        .runtime = GOAT_RUNTIME_GLIBC,
                                        .cpu_features = GOAT_CPU_SSE2};
    if (!strcmp(argv[3], "msvcrt") || !strcmp(argv[3], "ucrt")) {
        expected.os = GOAT_OS_WINDOWS;
        expected.runtime = !strcmp(argv[3], "ucrt") ? GOAT_RUNTIME_UCRT : GOAT_RUNTIME_MSVCRT;
    }
    goat_native_environment actual;
    char *diagnostic = NULL;
    bool ok =
        inspect_native_requirements(argv[1], &expected, resolve, argv[2], &actual, &diagnostic);
    if (ok)
        printf("%u.%u.%u.%u %u.%u.%u.%u\n",
               actual.os_version.components[0],
               actual.os_version.components[1],
               actual.os_version.components[2],
               actual.os_version.components[3],
               actual.runtime_version.components[0],
               actual.runtime_version.components[1],
               actual.runtime_version.components[2],
               actual.runtime_version.components[3]);
    else
        fprintf(stderr, "%s\n", diagnostic ? diagnostic : "Invalid arguments");
    FREE(diagnostic);
    free_fs_arguments(argc, argv);
    return ok ? 0 : 1;
}
