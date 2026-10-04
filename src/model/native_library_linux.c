/** @file native_library_linux.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Explicit-path Linux loading with immediate, local symbol resolution.
 */
#if defined(__linux__) && !defined(_XOPEN_SOURCE)
#    define _XOPEN_SOURCE 700
#endif
#include "native_library.h"

#ifdef __linux__
#    include <dlfcn.h>
#    include <errno.h>
#    include <stdlib.h>
#    include <string.h>

void *open_native_library_handle(const char *path, char **diagnostic) {
    char *absolute = realpath(path, NULL);
    if (!absolute) {
        *diagnostic = copy_native_library_diagnostic(strerror(errno));
        return NULL;
    }
    void *handle = dlopen(absolute, RTLD_NOW | RTLD_LOCAL);
    if (!handle)
        *diagnostic = copy_native_library_diagnostic(dlerror());
    free(absolute);
    return handle;
}

bool get_native_library_query(void *handle, goat_native_query_v1_t *query, char **diagnostic) {
    dlerror();
    void *address = dlsym(handle, "goat_native_query_v1");
    const char *error = dlerror();
    if (error || !address) {
        *diagnostic = copy_native_library_diagnostic(error ? error : "Missing native query");
        return false;
    }
    _Static_assert(sizeof(*query) == sizeof(address), "POSIX function pointer representation");
    memcpy(query, &address, sizeof(*query));
    return true;
}

void close_native_library_handle(void *handle) {
    dlclose(handle);
}
#endif
