/** @file native_library_windows.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Explicit-path Windows loading and exact ABI query lookup.
 */
#include "lib/windows_target.h"
#include "native_library.h"

#ifdef _WIN32
#    include "lib/allocate.h"

#    include <stdio.h>
#    include <string.h>
#    include <windows.h>

static char *last_error(void) {
    DWORD code = GetLastError();
    char message[512] = {0}, text[560];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL,
                   code,
                   0,
                   message,
                   sizeof(message),
                   NULL);
    snprintf(text, sizeof(text), "Win32 error %lu: %s", (unsigned long)code, message);
    return copy_native_library_diagnostic(text);
}

void *open_native_library_handle(const char *path, char **diagnostic) {
    DWORD size = GetFullPathNameA(path, 0, NULL, NULL);
    if (!size) {
        *diagnostic = last_error();
        return NULL;
    }
    char *absolute = ALLOC(size);
    DWORD length = GetFullPathNameA(path, size, absolute, NULL);
    if (!length || length >= size) {
        if (length)
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
        *diagnostic = last_error();
        FREE(absolute);
        return NULL;
    }
    HMODULE handle =
        LoadLibraryExA(absolute,
                       NULL,
                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!handle)
        *diagnostic = last_error();
    FREE(absolute);
    return handle;
}

bool get_native_library_query(void *handle, goat_native_query_v1_t *query, char **diagnostic) {
    FARPROC address = GetProcAddress((HMODULE)handle, "goat_native_query_v1");
    if (!address) {
        *diagnostic = last_error();
        return false;
    }
    _Static_assert(sizeof(*query) == sizeof(address), "Windows function pointer representation");
    memcpy(query, &address, sizeof(*query));
    return true;
}

void close_native_library_handle(void *handle) {
    FreeLibrary((HMODULE)handle);
}

bool native_stack_has_headroom(void) {
    void *position = __builtin_frame_address(0);
    MEMORY_BASIC_INFORMATION information;
    if (!VirtualQuery(position, &information, sizeof(information)))
        return false;
    uintptr_t here = (uintptr_t)position, low = (uintptr_t)information.AllocationBase;
    return here >= low && here - low >= 512 * 1024;
}
#endif
