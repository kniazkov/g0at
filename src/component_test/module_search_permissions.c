/** @file module_search_permissions.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Windows fixture token isolation, separate from VM opcode names.
 */
#ifdef _WIN32
#    include "lib/allocate.h"
#    include "lib/file_system_internal.h"

#    include <assert.h>
#    include <windows.h>

static HANDLE token, restricted;

void begin_module_search_permissions(void) {
    /* Hosted Windows runners may have backup privileges: exercise ordinary
     * filesystem permissions, not the elevated token's backup bypass. */
    assert(OpenProcessToken(GetCurrentProcess(),
                            TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY
                                | TOKEN_ADJUST_PRIVILEGES,
                            &token));
    assert(CreateRestrictedToken(token,
                                 DISABLE_MAX_PRIVILEGE,
                                 0,
                                 NULL,
                                 0,
                                 NULL,
                                 0,
                                 NULL,
                                 &restricted));
    assert(ImpersonateLoggedOnUser(restricted));
}

void end_module_search_permissions(void) {
    assert(RevertToSelf());
    assert(CloseHandle(restricted));
    assert(CloseHandle(token));
}
#endif

#ifdef _WIN32
unsigned long probe_module_search_access(const char *path, int directory) {
    wchar_t *wide = fs_wide(path);
    assert(wide);
    HANDLE handle = CreateFileW(wide,
                                directory ? FILE_LIST_DIRECTORY : GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL,
                                OPEN_EXISTING,
                                directory ? FILE_FLAG_BACKUP_SEMANTICS : 0,
                                NULL);
    DWORD error = handle == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    if (handle != INVALID_HANDLE_VALUE)
        assert(CloseHandle(handle));
    FREE(wide);
    return error;
}
#endif
