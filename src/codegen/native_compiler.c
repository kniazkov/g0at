/** @file native_compiler.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared compiler diagnostics and result lifetime.
 */
#include "native_compiler.h"

#include "lib/allocate.h"

#include <errno.h>
#include <stdio.h>

/** @brief Caps captured diagnostics at 64 KiB; file redirection avoids pipe deadlocks. */
void native_compiler_read_diagnostics(native_compile_result_t *result, const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        result->system_error = errno;
        result->status = NATIVE_COMPILE_IO_ERROR;
        return;
    }
    const size_t limit = 65536;
    result->diagnostics = ALLOC(limit + 1);
    size_t length = fread(result->diagnostics, 1, limit + 1, file);
    result->diagnostics_truncated = length > limit;
    result->diagnostics_length = length > limit ? limit : length;
    result->diagnostics[result->diagnostics_length] = '\0';
    if (ferror(file)) {
        result->system_error = errno ? errno : EIO;
        result->status = NATIVE_COMPILE_IO_ERROR;
    }
    fclose(file);
}

#if !defined(__linux__) && !defined(_WIN32)
native_compile_result_t
compile_native_library(const wchar_t *source, const char *compiler, const char *destination) {
    (void)source;
    (void)compiler;
    (void)destination;
    return (native_compile_result_t){.status = NATIVE_COMPILE_UNSUPPORTED, .exit_code = -1};
}

native_workspace_t *create_native_workspace(void) {
    return NULL;
}

void destroy_native_workspace(native_workspace_t *workspace) {
    (void)workspace;
}
#endif

void destroy_native_compile_result(native_compile_result_t *result) {
    FREE(result->diagnostics);
    result->diagnostics = NULL;
    result->diagnostics_length = 0;
}
