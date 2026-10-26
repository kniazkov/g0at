/** @file native_cleanup_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Real native consumer lifecycle with controlled cleaner and compiler subprocesses.
 */
#include "cli/launcher.h"
#include "codegen/native_compiler.h"
#include "lib/allocate.h"
#include "lib/cleanup_service.h"
#include "lib/file_system.h"
#include "lib/io.h"
#include "lib/process_channel.h"
#include "lib/string_ext.h"
#include "resources/messages.h"

#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include "lib/windows_target.h"

#    include <windows.h>
#else
#    include <unistd.h>
#endif

static unsigned long current_pid(void) {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return (unsigned long)getpid();
#endif
}

static void marker(const char *name, const char *value) {
    char *root = fs_environment("GOAT_NATIVE_TEST_MARKERS", NULL);
    char *path = root ? join_fs_path(root, name, NULL) : NULL;
    FILE *file = path ? FOPEN(path, "wb") : NULL;
    if (!file)
        exit(90);
    if (value)
        FPRINTF(file, "%s\n", value);
    else
        FPRINTF(file, "%lu\n", current_pid());
    FCLOSE(file);
    FREE(path);
    FREE(root);
}

static int fixture(int argc, char **argv) {
    if (argc == 5 && !strcmp(argv[1], CLEANUP_SERVICE_ARGUMENT)) {
        marker("cleaner.pid", NULL);
        const char *fault = getenv("GOAT_NATIVE_TEST_CLEANER_FAULT");
        if (fault && !strcmp(fault, "exit"))
            return 3;
        if (fault && !strcmp(fault, "silent")) {
            child_wait_parent((uintptr_t)strtoull(argv[2], NULL, 10), 0);
            return 0;
        }
        int status;
        if (!cleanup_service_main(argc, argv, &status))
            return 92;
        char completed[32];
        snprintf(completed, sizeof(completed), "%d", status);
        marker("cleaner.done", completed);
        return status;
    }
    if (argc > 1 && (!strcmp(argv[1], "--version") || !strcmp(argv[1], "-dumpmachine"))) {
        marker("compiler.query", NULL);
        return 3;
    }
    if (argc > 1 && !strcmp(argv[1], "worker")) {
        marker("worker.pid", NULL);
        for (;;)
            process_pause(100);
    }
    if (argc > 1 && !strcmp(argv[1], "-fake")) {
        const char *output = NULL;
        for (int i = 2; i + 1 < argc; i++)
            if (!strcmp(argv[i], "-o"))
                output = argv[i + 1];
        if (!output)
            return 93;
        marker("compiler.output", output);
        marker("compiler.pid", NULL);
        if (getenv("GOAT_NATIVE_TEST_BLOCK")) {
            const char *arguments[] = {"fixture", "worker", NULL};
            child_options_t options = {.arguments = arguments};
            child_process_t *child = start_child_process(&options);
            wait_child_process(child);
            destroy_child_process(child);
        } else {
            FILE *file = FOPEN(output, "wb");
            if (!file)
                return 94;
            FPUTS("saved-artifact", file);
            FCLOSE(file);
        }
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "build")) {
        native_build_context_t *context = create_native_build_context(NULL);
        const char *flags[] = {"-fake", NULL};
        native_compile_options_t options = {.arguments = flags};
        native_compile_result_t result =
            compile_native_library_in_context(context,
                                              U"int test(void){return 1;}",
                                              argv[0],
                                              argv[2],
                                              &options);
        int status = result.status == NATIVE_COMPILE_OK ? 0 : 1;
        destroy_native_compile_result(&result);
        release_native_build_context(context);
        return status;
    }
    init_unicode_locale();
    init_messages();
    if (!init_io())
        return 95;
    options_t *options = parse_options(argc, argv);
    if (!options)
        return 2;
    int status = go(options);
    destroy_options(options);
    return status >= 0 && status <= 255 ? status : 1;
}

int main(int argc, char **argv) {
    fs_error_t error = {0};
    if (!fs_arguments(&argc, &argv, &error))
        return 96;
    SETVBUF(stdout, NULL, _IONBF, 0);
    int status = fixture(argc, argv);
    free_fs_arguments(argc, argv);
    clear_fs_error(&error);
    print_list_of_memory_blocks();
    return status;
}
