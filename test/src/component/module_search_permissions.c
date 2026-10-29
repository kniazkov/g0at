/** @file module_search_permissions.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent OS access probes and isolated search fixture identities.
 */
#ifndef _WIN32
#    define _DEFAULT_SOURCE
#    define _POSIX_C_SOURCE 200809L
#endif
#undef NDEBUG
#include <assert.h>
#ifdef _WIN32
#    include "lib/files/file_system_internal.h"
#    include "lib/memory/allocate.h"

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

#ifdef _WIN32
void module_search_drop_privileges(void) {
    /* begin_module_search_permissions already isolates the Windows token. */
}

void module_search_retarget(const char *alias, const char *target) {
    assert(RevertToSelf());
    wchar_t *a = fs_wide(alias), *t = fs_wide(target);
    assert(a && t && DeleteFileW(a));
    BOOL ok = CreateSymbolicLinkW(a, t, 2);
    if (!ok && GetLastError() == ERROR_INVALID_PARAMETER)
        ok = CreateSymbolicLinkW(a, t, 0);
    assert(ok);
    FREE(a);
    FREE(t);
    assert(ImpersonateLoggedOnUser(restricted));
}
#else
#    include <dirent.h>
#    include <errno.h>
#    include <grp.h>
#    include <stdio.h>
#    include <unistd.h>

void module_search_drop_privileges(void) {
    if (geteuid() == 0) {
        assert(!setgroups(0, NULL));
        assert(!setgid(65534));
        assert(!setuid(65534));
    }
}

unsigned long probe_module_search_access(const char *path, int directory) {
    /* Independent OS access attempt, not a production filesystem helper. */
    if (directory) {
        DIR *d = opendir(path);
        if (!d)
            return (unsigned long)errno;
        errno = 0;
        while (readdir(d)) {
        }
        int error = errno;
        assert(!closedir(d));
        return (unsigned long)error;
    }
    FILE *f = fopen(path, "rb");
    if (!f)
        return (unsigned long)errno;
    unsigned char buffer[4096];
    while (fread(buffer, 1, sizeof(buffer), f)) {
    }
    int error = ferror(f) ? errno : 0;
    assert(!fclose(f));
    return (unsigned long)error;
}

void module_search_retarget(const char *alias, const char *target) {
    assert(!unlink(alias));
    assert(!symlink(target, alias));
}
#endif
