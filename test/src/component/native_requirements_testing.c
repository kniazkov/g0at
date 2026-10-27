/** @file native_requirements_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Native image inspection harness; never loads a library.
 */
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include "codegen/native_requirements.h"
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/threads/os_thread.h"
#include "model/native_library.h"

#include <stdio.h>
#include <string.h>
#ifdef __linux__
#    include <sys/mman.h>
#    include <unistd.h>
#endif

static char *resolve(const char *name, void *context) {
    return join_fs_path(context, name, NULL);
}

static void *stack_probe(void *context) {
    *(bool *)context = native_stack_has_headroom();
    return NULL;
}

int main(int argc, char **argv) {
    if (!fs_arguments(&argc, &argv, NULL))
        return 2;
#ifdef __linux__
    if (argc == 2 && !strcmp(argv[1], "--stack-blocked")) {
        goat_native_environment host;
        if (!goat_native_environment_detect(&host) || host.runtime != GOAT_RUNTIME_MUSL) {
            puts("not-musl");
            free_fs_arguments(argc, argv);
            return 0;
        }
        long page = sysconf(_SC_PAGESIZE);
        uintptr_t here = (uintptr_t)__builtin_frame_address(0);
        if (page <= 0 || here < 384 * 1024) {
            free_fs_arguments(argc, argv);
            return 2;
        }
        uintptr_t address = (here - 384 * 1024) / (uintptr_t)page * (uintptr_t)page;
        void *block = mmap((void *)address,
                           (size_t)page,
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                           -1,
                           0);
        if (block == MAP_FAILED || block != (void *)address) {
            if (block != MAP_FAILED)
                munmap(block, (size_t)page);
            free_fs_arguments(argc, argv);
            return 2;
        }
        bool available = native_stack_has_headroom();
        int status = munmap(block, (size_t)page);
        printf("%u\n", available ? 1u : 0u);
        free_fs_arguments(argc, argv);
        return status ? 2 : 0;
    }
#endif
    if (argc == 2
        && (!strcmp(argv[1], "--stack-main") || !strcmp(argv[1], "--stack-worker")
            || !strcmp(argv[1], "--stack-small"))) {
        bool available = false;
        if (!strcmp(argv[1], "--stack-main")) {
            stack_probe(&available);
        } else {
            os_thread_t *thread = NULL;
            size_t stack = !strcmp(argv[1], "--stack-small") ? 128 * 1024 : 0;
            if (create_os_thread(&thread, stack_probe, &available, stack) != OS_THREAD_OK
                || join_os_thread(thread, NULL) != OS_THREAD_OK
                || destroy_os_thread(thread) != OS_THREAD_OK) {
                free_fs_arguments(argc, argv);
                return 2;
            }
        }
        printf("%u\n", available ? 1u : 0u);
        free_fs_arguments(argc, argv);
        return 0;
    }
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
    if (!strcmp(argv[3], "musl") || !strcmp(argv[3], "musl-aarch64"))
        expected.runtime = GOAT_RUNTIME_MUSL;
    if (!strcmp(argv[3], "musl-aarch64")) {
        expected.architecture = GOAT_ARCH_AARCH64;
        expected.cpu_features = GOAT_CPU_FP | GOAT_CPU_NEON;
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
