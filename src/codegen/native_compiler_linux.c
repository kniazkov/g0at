/** @file native_compiler_linux.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Linux shared-library compilation, publication and temporary-file cleanup.
 */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#endif
#include "native_compiler.h"
#include "native_runtime.h"

#ifdef __linux__
#    include "lib/allocate.h"
#    include "lib/io.h"

#    include <errno.h>
#    include <fcntl.h>
#    include <stdio.h>
#    include <stdlib.h>
#    include <string.h>
#    include <sys/stat.h>
#    include <unistd.h>

static char *join(const char *first, const char *second) {
    size_t a = strlen(first), b = strlen(second);
    char *text = ALLOC(a + b + 1);
    memcpy(text, first, a);
    memcpy(text + a, second, b + 1);
    return text;
}

static void remove_file(native_compile_result_t *result, const char *path) {
    if (unlink(path) && errno != ENOENT && !result->cleanup_error)
        result->cleanup_error = errno;
}

native_compile_result_t
compile_native_library(const gchar_t *source, const char *compiler, const char *destination) {
    native_compile_result_t result = {.status = NATIVE_COMPILE_IO_ERROR, .exit_code = -1};
    if (!source || !destination || !destination[0] || !compiler || !compiler[0]) {
        result.system_error = EINVAL;
        return result;
    }
    /* Relative compiler operands must not be interpreted as options. */
    char *target = join(destination[0] == '/' ? "" : "./", destination);
    char *directory = join(target, ".goat-XXXXXX");
    if (!mkdtemp(directory)) {
        result.system_error = errno;
        FREE(directory);
        FREE(target);
        return result;
    }
    char *input = join(directory, "/module.c");
    char *output = join(directory, "/module.so");
    char *runtime = join(directory, "/" GOAT_NATIVE_ARCHIVE_NAME);
    if (!write_native_runtime_files(input) || !write_utf8_file(input, source)) {
        result.system_error = errno ? errno : EIO;
        goto cleanup;
    }
    const char *args[] = {compiler,
                          "-std=c11",
                          "-O2",
                          "-fPIC",
                          "-shared",
                          "-fno-fast-math",
                          "-ffp-contract=off",
                          "-Wl,-z,defs",
                          "-o",
                          output,
                          input,
                          runtime,
                          "-lm",
                          NULL};
    native_compiler_run(&result, args, 0);
    if (result.cleanup_error)
        goto release; /* Never delete a workspace while an unconfirmed tree can still write. */
    if (result.status != NATIVE_COMPILE_OK)
        goto cleanup;
    result.status = NATIVE_COMPILE_IO_ERROR;
    struct stat info;
    if (lstat(output, &info)) {
        result.system_error = errno;
        goto cleanup;
    }
    if (!S_ISREG(info.st_mode) || !info.st_size) {
        result.system_error = EIO;
        goto cleanup;
    }
    if (rename(output, target)) {
        result.system_error = errno;
        goto cleanup;
    }
    result.status = NATIVE_COMPILE_OK;
cleanup:
    remove_native_runtime_files(input);
    remove_file(&result, input);
    remove_file(&result, output);
    if (rmdir(directory) && !result.cleanup_error)
        result.cleanup_error = errno;
release:
    FREE(runtime);
    FREE(input);
    FREE(output);
    FREE(directory);
    FREE(target);
    return result;
}

native_workspace_t *create_native_workspace(void) {
    const char *base = getenv("TMPDIR");
    if (!base || !*base)
        base = "/tmp";
    char *directory = join(base, "/goat-native-XXXXXX");
    if (!mkdtemp(directory)) {
        FREE(directory);
        return NULL;
    }
    native_workspace_t *workspace = ALLOC(sizeof(*workspace));
    workspace->directory = directory;
    workspace->library = join(directory, "/module.so");
    return workspace;
}

void destroy_native_workspace(native_workspace_t *workspace) {
    if (!workspace)
        return;
    remove(workspace->library);
    rmdir(workspace->directory);
    FREE(workspace->library);
    FREE(workspace->directory);
    FREE(workspace);
}
#endif
