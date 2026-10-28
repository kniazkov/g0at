/** @file language_execution.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Owned UTF-8 files and inherited per-child environments.
 */
#include "test/support/language_execution.h"

#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"

#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#else
extern char **environ;
#endif
static bool release(void *pointer) {
    FREE(pointer);
    return true;
}

void *language_owned(scenario_run_t *run, void *pointer) {
    scenario_defer(run, release, pointer);
    scenario_require(run, pointer != NULL, "cannot allocate language fixture");
    return pointer;
}

const char *language_join(scenario_run_t *run, const char *directory, const char *name) {
    fs_error_t error = {0};
    char *path = join_fs_path(directory, name, &error);
    clear_fs_error(&error);
    return language_owned(run, path);
}

const char *language_read(scenario_run_t *run, const char *path, size_t *size) {
    fs_error_t error = {0};
    void *data = NULL;
    bool ok = read_fs_regular_file(path, 16 * 1024 * 1024, &data, size, &error);
    clear_fs_error(&error);
    if (data)
        scenario_defer(run, release, data);
    scenario_require(run, ok, "cannot read language fixture");
    return data;
}

void language_write(scenario_run_t *run, const char *path, const void *data, size_t size) {
    FILE *file = FOPEN(path, "wb");
    if (!file)
        scenario_require(run, false, "cannot open language output");
    bool ok = fwrite(data, 1, size, file) == size;
    if (fclose(file))
        ok = false;
    scenario_require(run, ok, "cannot write language output");
}

void language_directory(scenario_run_t *run, const char *path) {
    fs_error_t error = {0};
    bool ok = create_fs_directory(path, &error);
    clear_fs_error(&error);
    scenario_require(run, ok, "cannot create language fixture directory");
}

void language_remove(scenario_run_t *run, const char *path) {
    fs_error_t error = {0};
    bool ok = remove_fs_entry(path, &error);
    clear_fs_error(&error);
    scenario_require(run, ok, "cannot remove saved-program source");
}

bool language_exists(scenario_run_t *run, const char *path) {
    fs_error_t error = {0};
    fs_info_t info;
    bool ok = inspect_fs_path(path, true, &info, &error);
    bool missing = error.status == FS_NOT_FOUND;
    clear_fs_error(&error);
    scenario_require(run, ok || missing, "cannot inspect language fixture");
    return ok && info.kind == FS_FILE;
}

static bool compiler_variable(const char *value) {
#ifdef _WIN32
    return !_strnicmp(value, "CC=", 3);
#else
    return !strncmp(value, "CC=", 3);
#endif
}

static bool release_environment(void *pointer) {
    char **values = pointer;
    if (values) {
        for (size_t i = 0; values[i]; ++i)
            FREE(values[i]);
        FREE(values);
    }
    return true;
}

const char *const *language_environment(scenario_run_t *run, bool no_compiler) {
    if (!no_compiler)
        return NULL;
    size_t count = 0, index = 0;
#ifdef _WIN32
    wchar_t *block = GetEnvironmentStringsW();
    if (!block)
        scenario_require(run, false, "cannot snapshot environment");
    for (wchar_t *p = block; *p; p += wcslen(p) + 1)
        if (*p != L'=')
            count++;
    /* Convert before fail-fast operations, so the native block is always freed. */
    char **values = CALLOC((count + 2) * sizeof(*values));
    bool ok = true;
    for (wchar_t *p = block; *p; p += wcslen(p) + 1) {
        if (*p == L'=')
            continue;
        int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, p, -1, NULL, 0, NULL, NULL);
        if (!n) {
            ok = false;
            break;
        }
        char *value = ALLOC((size_t)n);
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, p, -1, value, n, NULL, NULL)) {
            FREE(value);
            ok = false;
            break;
        }
        if (compiler_variable(value))
            FREE(value);
        else
            values[index++] = value;
    }
    FreeEnvironmentStringsW(block);
    scenario_defer(run, release_environment, values);
    scenario_require(run, ok, "invalid Unicode environment");
#else
    for (char **p = environ; p && *p; ++p)
        count++;
    char **values = CALLOC((count + 2) * sizeof(*values));
    scenario_defer(run, release_environment, values);
    for (char **p = environ; p && *p; ++p) {
        if (compiler_variable(*p))
            continue;
        char *copy = ALLOC(strlen(*p) + 1);
        strcpy(copy, *p);
        values[index++] = copy;
    }
#endif
    const char *missing = "CC=goat-missing-compiler-for-test";
    values[index] = ALLOC(strlen(missing) + 1);
    strcpy(values[index], missing);
    scenario_checkpoint(run);
    return (const char *const *)values;
}

scenario_command_result_t language_command(scenario_run_t *run,
                                           const char *exe,
                                           const char *const *arguments,
                                           const char *const *environment) {
    child_options_t options = {.executable = exe,
                               .arguments = arguments,
                               .environment = environment,
                               .empty_stdin = true};
    return scenario_command_capture(run, &options);
}

void language_check(scenario_run_t *run,
                    scenario_command_result_t result,
                    int status,
                    const char *expected,
                    size_t size,
                    const char *error,
                    size_t error_size) {
    scenario_require(run, result.exit_code == status, "unexpected language command status");
    scenario_require_bytes(run,
                           "language stdout",
                           expected,
                           size,
                           result.out,
                           result.out_size,
                           true);
    scenario_require_bytes(run,
                           "language stderr",
                           error,
                           error_size,
                           result.err,
                           result.err_size,
                           true);
}
