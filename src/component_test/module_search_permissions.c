/** @file module_search_permissions.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Windows fixture token isolation, separate from VM opcode names.
 */
#ifdef _WIN32
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
