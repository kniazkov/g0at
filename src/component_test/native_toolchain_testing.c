/** @file native_toolchain_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Compiler discovery harness and controllable compiler subprocess.
 */
#include "codegen/native_toolchain.h"
#include "lib/allocate.h"
#include "lib/file_system.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <unistd.h>
#endif

static int compiler(int argc, char **argv) {
    char *real = fs_environment("GOAT_TOOLCHAIN_REAL", NULL);
    assert(real);
    char *log = fs_environment("GOAT_TOOLCHAIN_LOG", NULL);
    if (log) {
        FILE *file = FOPEN(log, "ab");
        assert(file);
        FPRINTF(file, "%s %s\n", argv[0], argv[1]);
        assert(!FCLOSE(file));
    }
    FREE(log);
    char *failure = fs_environment("GOAT_TOOLCHAIN_FAIL", NULL);
    const char *basename = argv[0];
    for (const char *p = argv[0]; *p; p++)
        if (*p == '/' || *p == '\\')
            basename = p + 1;
    bool fail = failure && !strcmp(basename, failure);
    FREE(failure);
    if (fail) {
        FREE(real);
        fputs("controlled compiler failure\n", stderr);
        return 2;
    }
    char *mode = fs_environment("GOAT_TOOLCHAIN_MODE", NULL);
    char *mode_for = fs_environment("GOAT_TOOLCHAIN_MODE_FOR", NULL);
    if (mode_for && strcmp(mode_for, basename)) {
        FREE(mode);
        mode = NULL;
    }
    FREE(mode_for);
    bool preprocess = false;
    for (int i = 1; i < argc; i++)
        preprocess |= !strcmp(argv[i], "-E");
    if (mode
        && ((!strcmp(mode, "hang-query") && preprocess)
            || (!strcmp(mode, "hang-link") && !preprocess
                && !strstr(argv[1], "-print-file-name=")))) {
#ifdef _WIN32
        Sleep(30000);
#else
        sleep(30);
#endif
    }
    if (mode && !strcmp(mode, "link-fail") && !preprocess
        && !strstr(argv[1], "-print-file-name=")) {
        FREE(mode);
        FREE(real);
        fputs("controlled link failure\n", stderr);
        return 3;
    }
    bool hide_trace = mode && !strcmp(mode, "hide-trace") && !preprocess;
    FREE(mode);
    /* Test-only control library: a real versioned libc dependency, never patched metadata. */
    bool module = false;
    for (int i = 1; i < argc; i++)
        module |= !strcmp(argv[i], "./module.c");
    char *extra_source = module ? fs_environment("GOAT_TOOLCHAIN_EXTRA_SOURCE", NULL) : NULL;
    const char **args = ALLOC(((size_t)argc + 2) * sizeof(*args));
    args[0] = real;
    for (int i = 1; i < argc; i++)
        args[i] = argv[i];
    size_t count = (size_t)argc;
    if (extra_source)
        args[count++] = extra_source;
    args[count] = NULL;
    /* Optional exact argv audit; the legacy discovery log stays unchanged. */
    char *arguments_log = fs_environment("GOAT_TOOLCHAIN_ARGUMENTS_LOG", NULL);
    if (arguments_log) {
        FILE *file = FOPEN(arguments_log, "ab");
        assert(file);
        for (size_t i = 1; i < count; i++) {
            assert(FWRITE(args[i], 1, strlen(args[i]) + 1, file) == strlen(args[i]) + 1);
        }
        assert(fputc('\n', file) != EOF);
        assert(!FCLOSE(file));
    }
    FREE(arguments_log);
    child_options_t options = {.executable = real,
                               .arguments = args,
                               .capture_output = true,
                               .empty_stdin = true};
    child_process_t *child = start_child_process(&options);
    const child_result_t *result = wait_child_process(child);
    if (!hide_trace)
        FWRITE(result->out.data, 1, result->out.size, stdout);
    FWRITE(result->err.data, 1, result->err.size, stderr);
    int code = result->status == CHILD_EXITED ? (int)result->exit_code : 4;
    destroy_child_process(child);
    FREE(args);
    FREE(extra_source);
    FREE(real);
    return code;
}

static void target_profiles(void) {
    goat_native_environment target;
    assert(native_toolchain_target("linux-x86_64-musl", &target));
    assert(target.os == GOAT_OS_LINUX && target.runtime == GOAT_RUNTIME_MUSL
           && target.architecture == GOAT_ARCH_X86_64 && target.bits == 64
           && target.cpu_features == GOAT_CPU_SSE2);
    assert(native_toolchain_target("linux-x86_64-musl+avx2", &target));
    assert(target.runtime == GOAT_RUNTIME_MUSL && (target.cpu_features & GOAT_CPU_AVX2));
    assert(native_toolchain_target("linux-aarch64-musl", &target));
    assert(target.os == GOAT_OS_LINUX && target.runtime == GOAT_RUNTIME_MUSL
           && target.architecture == GOAT_ARCH_AARCH64 && target.bits == 64
           && target.cpu_features == (GOAT_CPU_FP | GOAT_CPU_NEON));
    assert(native_toolchain_target("linux-aarch64-musl+sha2", &target));
    assert(target.runtime == GOAT_RUNTIME_MUSL
           && target.cpu_features == (GOAT_CPU_FP | GOAT_CPU_NEON | GOAT_CPU_SHA1 | GOAT_CPU_SHA2));
    assert(!native_toolchain_target("linux-aarch64-musl+crypto", &target));
    assert(!native_toolchain_target("linux-aarch64-musl+avx2", &target));
    assert(!native_toolchain_target("linux-x86_64-musl+sha2", &target));
}

static int execute(int argc, char **argv) {
    int code;
    if (cleanup_service_main(argc, argv, &code))
        return code;
    if (argc > 1 && argv[1][0] == '-')
        return compiler(argc, argv);
    assert(argc == 6);
    target_profiles();
    cleanup_service_t *cleanup = create_cleanup_service(NULL);
    assert(cleanup);
    native_toolchain_context_t *context = create_native_toolchain_context(cleanup);
    const char *cc = strcmp(argv[2], "PATH") ? argv[2] : NULL;
    const char *target = strcmp(argv[3], "all") ? argv[3] : NULL;
    uint64_t timeout = strtoull(argv[4], NULL, 10);
    bool enumerate = !strcmp(argv[1], "list");
    unsigned repetitions = (unsigned)strtoul(argv[5], NULL, 10);
    for (unsigned j = 0; j < repetitions; j++) {
        native_toolchain_search_t search =
            find_native_toolchains(context,
                                   target,
                                   cc,
                                   timeout + (!strcmp(argv[1], "vary-timeout") ? j : 0),
                                   enumerate);
        printf("round=%u count=%zu selected=%s\n",
               j,
               search.count,
               search.selected ? search.selected->compiler : "none");
        if (search.diagnostic)
            printf("diagnostic=%s\n", search.diagnostic);
        for (size_t i = 0; i < search.count; i++) {
            const native_toolchain_t *item = search.items[i];
            printf("candidate=%s target=%s available=%d timeout=%d name=%s version=%s\n",
                   item->compiler,
                   item->target,
                   item->available,
                   item->timed_out,
                   item->name ? item->name : "unknown",
                   item->version ? item->version : "unknown");
            if (item->diagnostic)
                printf("reason=%s\n", item->diagnostic);
        }
        destroy_native_toolchain_search(&search);
    }
    destroy_native_toolchain_context(context);
    cleanup_result_t result = finish_cleanup_service(cleanup);
    assert(result.status == CLEANUP_OK || result.status == CLEANUP_CLOSED);
    destroy_cleanup_service(cleanup);
    return 0;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    if (!fs_arguments(&argc, &argv, NULL))
        return 1;
#endif
    int code = execute(argc, argv);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return code;
}
