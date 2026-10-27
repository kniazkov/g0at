/** @file scenario_context_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent scenario workspace lifecycle checks, active in Release builds.
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/threads/os_thread.h"
#include "test/support/scenario.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "Failed line %d: %s\n", __LINE__, #condition);                         \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static char *copy_text(const char *text) {
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);
    if (copy)
        memcpy(copy, text, size);
    return copy;
}

static bool exists(const char *path) {
    fs_info_t info;
    fs_error_t error = {0};
    bool found = inspect_fs_path(path, false, &info, &error);
    clear_fs_error(&error);
    return found;
}

static bool marker(const char *directory) {
    fs_error_t error = {0};
    char *path = join_fs_path(directory, "marker.bin", &error);
    FILE *file = path ? FOPEN(path, "wb") : NULL;
    static const unsigned char bytes[] = {0, 1, 2, 255, '\n'};
    bool ok = file && fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
    if (file && fclose(file))
        ok = false;
    FREE(path);
    clear_fs_error(&error);
    return ok;
}

static bool diagnostic_contains(FILE *stream, const char *text) {
    char buffer[8192];
    if (fflush(stream) || fseek(stream, 0, SEEK_SET))
        return false;
    size_t size = fread(buffer, 1, sizeof(buffer) - 1, stream);
    buffer[size] = 0;
    return strstr(buffer, text) != NULL;
}

static int lifecycle(scenario_options_t options, bool success, bool keep) {
    FILE *diagnostics = tmpfile();
    CHECK(diagnostics);
    options.keep_work = keep;
    options.diagnostics = diagnostics;
    char *name_copy = copy_text(options.name);
    char *build_copy = copy_text(options.build_directory);
    char *data_copy = copy_text(options.data_directory);
    CHECK(name_copy && build_copy && data_copy);
    scenario_options_t copied = options;
    copied.name = name_copy;
    copied.build_directory = build_copy;
    copied.data_directory = data_copy;
    scenario_context_t *context = scenario_create(&copied);
    CHECK(context);
    memset(name_copy, 'x', strlen(name_copy));
    memset(build_copy, 'x', strlen(build_copy));
    memset(data_copy, 'x', strlen(data_copy));
    free(name_copy);
    free(build_copy);
    free(data_copy);
    CHECK(strcmp(scenario_name(context), options.name) == 0);
    fs_error_t error = {0};
    char *data = canonical_fs_path(options.data_directory, &error);
    CHECK(data && strcmp(scenario_data_directory(context), data) == 0);
    FREE(data);
    char *path = copy_text(scenario_work_directory(context));
    CHECK(path && marker(path));
    CHECK(scenario_finish(context, success) == success);
    CHECK(exists(path) == (!success || keep));
    if (!success || keep) {
        CHECK(diagnostic_contains(diagnostics, path));
        CHECK(remove_fs_tree(path, &error));
    }
    clear_fs_error(&error);
    free(path);
    CHECK(fclose(diagnostics) == 0);
    return 0;
}

static int isolation(scenario_options_t options) {
    char *paths[12] = {0};
    for (size_t i = 0; i < 12; i++) {
        scenario_context_t *a = scenario_create(&options);
        scenario_context_t *b = scenario_create(&options);
        CHECK(a && b);
        paths[i] = copy_text(scenario_work_directory(a));
        char *other = copy_text(scenario_work_directory(b));
        CHECK(paths[i] && other && strcmp(paths[i], other));
        for (size_t j = 0; j < i; j++)
            CHECK(strcmp(paths[i], paths[j]));
        CHECK(marker(paths[i]) && marker(other));
        CHECK(scenario_finish(a, true));
        CHECK(!exists(paths[i]) && exists(other));
        CHECK(scenario_finish(b, true));
        CHECK(!exists(other));
        free(other);
    }
    for (size_t i = 0; i < 12; i++)
        free(paths[i]);
    return 0;
}

typedef struct {
    scenario_options_t options;
    scenario_context_t *context;
} parallel_run_t;

static void *create_parallel(void *argument) {
    parallel_run_t *run = argument;
    run->context = scenario_create(&run->options);
    return NULL;
}

static int parallel(scenario_options_t options) {
    parallel_run_t runs[8] = {0};
    os_thread_t *threads[8] = {0};
    char *paths[8] = {0};
    for (size_t i = 0; i < 8; i++) {
        runs[i].options = options;
        CHECK(create_os_thread(&threads[i], create_parallel, &runs[i], 0) == OS_THREAD_OK);
    }
    for (size_t i = 0; i < 8; i++) {
        CHECK(join_os_thread(threads[i], NULL) == OS_THREAD_OK);
        CHECK(destroy_os_thread(threads[i]) == OS_THREAD_OK);
        CHECK(runs[i].context);
        paths[i] = copy_text(scenario_work_directory(runs[i].context));
        CHECK(paths[i] && marker(paths[i]));
        for (size_t j = 0; j < i; j++)
            CHECK(strcmp(paths[i], paths[j]));
    }
    for (size_t i = 0; i < 8; i++) {
        CHECK(scenario_finish(runs[i].context, true));
        CHECK(!exists(paths[i]));
        for (size_t j = i + 1; j < 8; j++)
            CHECK(exists(paths[j]));
        free(paths[i]);
    }
    return 0;
}

static int cleanup_error(scenario_options_t options) {
    FILE *diagnostics = tmpfile();
    CHECK(diagnostics);
    options.diagnostics = diagnostics;
    scenario_context_t *context = scenario_create(&options);
    CHECK(context);
    char *path = copy_text(scenario_work_directory(context));
    CHECK(path);
    fs_error_t error = {0};
    CHECK(remove_fs_tree(path, &error));
    FILE *replacement = FOPEN(path, "wb");
    CHECK(replacement && fputs("not owned", replacement) >= 0);
    CHECK(fclose(replacement) == 0);
    CHECK(!scenario_finish(context, true));
    CHECK(exists(path));
    CHECK(diagnostic_contains(diagnostics, path));
    CHECK(diagnostic_contains(diagnostics, "cleanup failed"));
    CHECK(remove_fs_entry(path, &error));
    clear_fs_error(&error);
    free(path);
    CHECK(fclose(diagnostics) == 0);
    return 0;
}

static int invalid(scenario_options_t options) {
    CHECK(scenario_keep_work_argument("--keep-work"));
    CHECK(!scenario_keep_work_argument(NULL));
    CHECK(!scenario_keep_work_argument("--keep-work=yes"));
    CHECK(!scenario_keep_work_argument("--keep"));
    CHECK(!scenario_create(NULL));
    fs_error_t error = {0};
    char *prefix = join_fs_path(options.build_directory, "nested-data", &error);
    CHECK(prefix);
    char *nested = create_fs_temporary(prefix, true, NULL, &error);
    FREE(prefix);
    CHECK(nested);
    scenario_options_t overlap = options;
    overlap.data_directory = nested;
    CHECK(!scenario_create(&overlap));
    overlap.build_directory = nested;
    overlap.data_directory = options.build_directory;
    CHECK(!scenario_create(&overlap));
    CHECK(remove_fs_tree(nested, &error));
    FREE(nested);
    clear_fs_error(&error);
    options.name = NULL;
    CHECK(!scenario_create(&options));
    options.name = "";
    CHECK(!scenario_create(&options));
    options.name = "valid";
    const char *build = options.build_directory;
    options.build_directory = options.data_directory;
    CHECK(!scenario_create(&options));
    options.build_directory = build;
    options.data_directory = NULL;
    CHECK(!scenario_create(&options));
    options.build_directory = NULL;
    CHECK(!scenario_create(&options));
    return 0;
}

static int run(int argc, char **argv) {
    if (argc != 4)
        return 2;
    scenario_options_t options = {"same scenario", argv[2], argv[3], false, stderr};
    if (!strcmp(argv[1], "success"))
        return lifecycle(options, true, false);
    if (!strcmp(argv[1], "failure"))
        return lifecycle(options, false, false);
    if (!strcmp(argv[1], "keep-work"))
        return lifecycle(options, true, true);
    if (!strcmp(argv[1], "isolation"))
        return isolation(options);
    if (!strcmp(argv[1], "parallel"))
        return parallel(options);
    if (!strcmp(argv[1], "cleanup-error"))
        return cleanup_error(options);
    if (!strcmp(argv[1], "invalid"))
        return invalid(options);
    return 2;
}

int main(int argc, char **argv) {
    fs_error_t error = {0};
    if (!fs_arguments(&argc, &argv, &error)) {
        clear_fs_error(&error);
        return 2;
    }
    int result = run(argc, argv);
    free_fs_arguments(argc, argv);
    clear_fs_error(&error);
    return result;
}
