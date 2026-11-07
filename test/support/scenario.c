/** @file scenario.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Test-only work directory ownership and retention policy.
 */
#include "test/support/scenario.h"

#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/platform/windows_target.h"

#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#endif

struct scenario_context_t {
    char *name, *work, *data;
    fs_info_t identity;
    FILE *diagnostics;
    bool keep;
};

static char *copy_text(const char *text) {
    size_t size = strlen(text) + 1;
    char *copy = ALLOC(size);
    if (copy)
        memcpy(copy, text, size);
    return copy;
}

static bool contains_path(const char *parent, const char *path) {
    size_t length = strlen(parent);
#ifdef _WIN32
    if (_strnicmp(parent, path, length))
        return false;
#else
    if (strncmp(parent, path, length))
        return false;
#endif
    return !path[length] || parent[length - 1] == '/' || path[length] == '/';
}

static void release_context(scenario_context_t *context) {
    FREE(context->name);
    FREE(context->work);
    FREE(context->data);
    FREE(context);
}

scenario_context_t *scenario_create(const scenario_options_t *options) {
    FILE *out = options && options->diagnostics ? options->diagnostics : stderr;
    fs_error_t error = {0};
    fs_info_t info;
    char *build = NULL, *prefix = NULL;
    scenario_context_t *context = NULL;
    if (!options || !options->name || !*options->name || !options->build_directory
        || !options->data_directory)
        goto failed;
    context = CALLOC(sizeof(*context));
    if (!context)
        goto failed;
    context->diagnostics = out;
    context->keep = options->keep_work;
    context->name = copy_text(options->name);
    build = canonical_fs_path(options->build_directory, &error);
    context->data = canonical_fs_path(options->data_directory, &error);
    if (!context->name || !build || !context->data || !inspect_fs_path(build, false, &info, &error)
        || info.kind != FS_DIRECTORY || !inspect_fs_path(context->data, false, &info, &error)
        || info.kind != FS_DIRECTORY || contains_path(context->data, build)
        || contains_path(build, context->data))
        goto failed;
    prefix = join_fs_path(build, "scenario", &error);
    if (!prefix)
        goto failed;
    context->work = create_fs_temporary(prefix, true, NULL, &error);
    if (!context->work)
        goto failed;
    if (!inspect_fs_path(context->work, false, &context->identity, &error))
        goto failed;
    FREE(prefix);
    FREE(build);
    clear_fs_error(&error);
    return context;
failed:
    fprintf(out,
            "scenario creation failed: %s (status %d, native %lu)%s%s\n",
            error.operation ? error.operation : "invalid options or directories",
            error.status,
            error.native_code,
            context && context->work ? "; retained work: " : "",
            context && context->work ? context->work : "");
    if (context)
        release_context(context);
    FREE(prefix);
    FREE(build);
    clear_fs_error(&error);
    return NULL;
}

const char *scenario_name(const scenario_context_t *context) {
    return context->name;
}

const char *scenario_work_directory(const scenario_context_t *context) {
    return context->work;
}

const char *scenario_data_directory(const scenario_context_t *context) {
    return context->data;
}

bool scenario_keep_work_argument(const char *argument) {
    return argument && !strcmp(argument, "--keep-work");
}

/* Retry only Windows sharing/lock violations. Process completion does not imply
 * immediate release of every filesystem handle (e.g. an external image scanner).
 * Recheck the workspace identity before each attempt; never retry permission or
 * identity errors, and retain diagnostics when the bounded budget expires. */
static bool remove_workspace(scenario_context_t *context, fs_error_t *error) {
#ifdef _WIN32
    ULONGLONG deadline = GetTickCount64() + 2000;
#endif
    for (;;) {
        clear_fs_error(error);
        fs_info_t current;
        bool owned = inspect_fs_path(context->work, false, &current, error);
        if (owned
            && (current.kind != FS_DIRECTORY || current.device != context->identity.device
                || current.identity != context->identity.identity)) {
            error->status = FS_CHANGED;
            error->operation = "workspace identity changed";
            return false;
        }
        if (owned && remove_fs_tree(context->work, error))
            return true;
#ifdef _WIN32
        if ((error->native_code == ERROR_SHARING_VIOLATION
             || error->native_code == ERROR_LOCK_VIOLATION)
            && GetTickCount64() < deadline) {
            Sleep(10);
            continue;
        }
#endif
        return false;
    }
}

bool scenario_finish(scenario_context_t *context, bool success) {
    if (!context)
        return false;
    if (success && !context->keep) {
        fs_error_t error = {0};
        if (!remove_workspace(context, &error)) {
            fprintf(context->diagnostics,
                    "scenario '%s': cleanup failed (status %d, native %lu, operation %s, path %s); "
                    "retained work: %s\n",
                    context->name,
                    error.status,
                    error.native_code,
                    error.operation ? error.operation : "unknown",
                    error.path ? error.path : context->work,
                    context->work);
            success = false;
        }
        clear_fs_error(&error);
    } else {
        fprintf(context->diagnostics,
                "scenario '%s': %s; retained work: %s\n",
                context->name,
                success ? "--keep-work" : "failed",
                context->work);
    }
    release_context(context);
    return success;
}
