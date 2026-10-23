/** @file native_compiler_windows.c
 * @copyright 2026 Ivan Kniazkov
 * @brief MinGW DLL compilation without a shell; failed builds preserve the previous DLL.
 */
#include "lib/windows_target.h"

#ifdef _WIN32
#    include <windows.h>
#endif
#include "native_compiler.h"
#include "native_runtime.h"

#ifdef _WIN32
#    include "lib/allocate.h"
#    include "lib/io.h"
#    include "lib/windows_command_line.h"

#    include <bcrypt.h>
#    include <errno.h>
#    include <stdio.h>
#    include <string.h>

static char *join(const char *a, const char *b) {
    size_t n = strlen(a), m = strlen(b);
    char *text = ALLOC(n + m + 1);
    memcpy(text, a, n);
    memcpy(text + n, b, m + 1);
    return text;
}

/** @brief Creates an exclusive sibling directory; its ACL is inherited from the parent. */
static char *create_directory(const char *target) {
    char *directory = ALLOC(strlen(target) + 40);
    for (unsigned attempt = 0; attempt < 32; attempt++) {
        uint32_t random[4];
        if (BCryptGenRandom(NULL,
                            (PUCHAR)random,
                            sizeof(random),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG)) {
            SetLastError(ERROR_GEN_FAILURE);
            break;
        }
        sprintf(directory,
                "%s.goat-%08x%08x%08x%08x",
                target,
                (unsigned)random[0],
                (unsigned)random[1],
                (unsigned)random[2],
                (unsigned)random[3]);
        if (CreateDirectoryA(directory, NULL))
            return directory;
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            break;
    }
    DWORD error = GetLastError();
    FREE(directory);
    SetLastError(error);
    return NULL;
}

static void remove_file(native_compile_result_t *result, const char *path) {
    if (!DeleteFileA(path)) {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && !result->windows_cleanup_error)
            result->windows_cleanup_error = error;
    }
}

native_compile_result_t
compile_native_library(const gchar_t *source, const char *compiler, const char *destination) {
    native_compile_result_t result = {.status = NATIVE_COMPILE_IO_ERROR, .exit_code = -1};
    if (!source || !compiler || !compiler[0] || !destination || !destination[0]) {
        result.system_error = EINVAL;
        return result;
    }
    DWORD size = GetFullPathNameA(destination, 0, NULL, NULL);
    if (!size) {
        result.windows_error = GetLastError();
        return result;
    }
    char *target = ALLOC(size);
    DWORD length = GetFullPathNameA(destination, size, target, NULL);
    if (!length || length >= size) {
        result.windows_error = length ? ERROR_INSUFFICIENT_BUFFER : GetLastError();
        FREE(target);
        return result;
    }
    char *directory = create_directory(target);
    if (!directory) {
        result.windows_error = GetLastError();
        FREE(target);
        return result;
    }
    char *input = join(directory, "\\module.c");
    /* PE's export directory stores this basename; moving module.dll would break imports. */
    const char *name = target;
    for (const char *p = target; *p; p++)
        if (*p == '\\' || *p == '/')
            name = p + 1;
    char *output = ALLOC(strlen(directory) + strlen(name) + 2);
    sprintf(output, "%s\\%s", directory, name);
    char *runtime = join(directory, "\\" GOAT_NATIVE_ARCHIVE_NAME);
    if (!write_native_runtime_files(input) || !write_utf8_file(input, source)) {
        result.system_error = errno ? errno : EIO;
        goto cleanup;
    }
    const char *args[] = {compiler,
                          "-std=c11",
                          "-O2",
                          "-shared",
                          /* Keep MinGW runtimes inside the DLL, including POSIX threads. */
                          "-static",
                          "-static-libgcc",
                          "-DGOAT_NATIVE_BUILD",
#    ifdef _WIN64
                          "-m64",
#    else
                          "-m32",
#    endif
                          "-fno-fast-math",
                          "-ffp-contract=off",
                          "-Wl,--no-undefined",
                          "-Wl,--exclude-all-symbols",
                          "-o",
                          output,
                          input,
                          runtime,
                          "-lm",
                          NULL};
    native_compiler_run(&result, args, 0);
    if (result.windows_cleanup_error)
        goto release;
    if (result.status != NATIVE_COMPILE_OK)
        goto cleanup;
    result.status = NATIVE_COMPILE_IO_ERROR;
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(output, GetFileExInfoStandard, &info)) {
        result.windows_error = GetLastError();
        goto cleanup;
    }
    if ((info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        || (!info.nFileSizeHigh && !info.nFileSizeLow)) {
        result.windows_error = ERROR_INVALID_DATA;
        goto cleanup;
    }
    if (!MoveFileExA(output, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        result.windows_error = GetLastError();
        goto cleanup;
    }
    result.status = NATIVE_COMPILE_OK;
cleanup:
    remove_native_runtime_files(input);
    remove_file(&result, input);
    remove_file(&result, output);
    if (!RemoveDirectoryA(directory) && !result.windows_cleanup_error)
        result.windows_cleanup_error = GetLastError();
release:
    FREE(runtime);
    FREE(input);
    FREE(output);
    FREE(directory);
    FREE(target);
    return result;
}

native_workspace_t *create_native_workspace(void) {
    DWORD size = GetTempPathA(0, NULL);
    if (!size)
        return NULL;
    char *base = ALLOC((size_t)size + 1);
    DWORD length = GetTempPathA(size + 1, base);
    if (!length || length > size) {
        FREE(base);
        return NULL;
    }
    char *prefix = join(base, "goat-native");
    FREE(base);
    char *directory = create_directory(prefix);
    FREE(prefix);
    if (!directory)
        return NULL;
    native_workspace_t *workspace = ALLOC(sizeof(*workspace));
    workspace->directory = directory;
    workspace->library = join(directory, "/module.dll");
    return workspace;
}

void destroy_native_workspace(native_workspace_t *workspace) {
    if (!workspace)
        return;
    remove(workspace->library);
    RemoveDirectoryA(workspace->directory);
    FREE(workspace->library);
    FREE(workspace->directory);
    FREE(workspace);
}
#endif
