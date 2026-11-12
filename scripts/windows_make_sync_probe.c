/* Copyright 2026 Ivan Kniazkov
 * Distributed under the MIT license; see LICENSE.txt.
 */
/* Read-only inherited-handle inspection; self-control creates only a local mutex. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <winternl.h>
typedef NTSTATUS(NTAPI *query_object_fn)(HANDLE, int, PVOID, ULONG, PULONG);

int main(int argc, char **argv) {
    const char *flags = getenv("MAKEFLAGS");
    const char *p;
    char *end;
    unsigned long long number;
    DWORD info = 0, error;
    HANDLE handle;
    HMODULE ntdll;
    query_object_fn query;

    union {
        void *align;
        unsigned char bytes[4096];
    } buffer;

    ULONG needed = 0;
    NTSTATUS status;
    const char *label = argc > 1 ? argv[1] : "probe";
    int control = !strcmp(label, "self_control");
    int expected_mutex = control || (argc > 2 && !strcmp(argv[2], "mutex"));
    int expected_absent = argc > 2 && !strcmp(argv[2], "absent");
    int type_is_mutex = 0;
    printf("label=%s pid=%lu MAKEFLAGS=[%s] MAKELEVEL=[%s]\n",
           label,
           (unsigned long)GetCurrentProcessId(),
           flags ? flags : "",
           getenv("MAKELEVEL") ? getenv("MAKELEVEL") : "");
    p = flags ? strstr(flags, "--sync-mutex=") : NULL;
    if (!p && !control) {
        puts("sync_handle=absent");
        return expected_mutex ? 3 : 0;
    }
    if (control) {
        handle = CreateMutexW(NULL, FALSE, NULL);
        if (!handle)
            return 4;
    } else {
        p += strlen("--sync-mutex=");
        errno = 0;
        number = strtoull(p, &end, 0);
        if (errno || end == p || (*end && *end != ' ')) {
            puts("sync_handle=parse_error");
            return 2;
        }
        handle = (HANDLE)(uintptr_t)number;
    }
    if (expected_absent) {
        puts("baseline_failed=unexpected_sync_mutex");
        return 3;
    }
    SetLastError(0);
    if (!GetHandleInformation(handle, &info)) {
        error = GetLastError();
        printf("sync_handle=%p valid=false error=%lu\n", handle, (unsigned long)error);
        return expected_mutex ? 3 : 0; /* non-control observations do not fail */
    }
    printf("sync_handle=%p valid=true inherit=%d\n", handle, !!(info & HANDLE_FLAG_INHERIT));
    ntdll = GetModuleHandleW(L"ntdll.dll");
    query = (query_object_fn)(void *)GetProcAddress(ntdll, "NtQueryObject");
    if (!query) {
        puts("object_type=unavailable");
        if (control)
            CloseHandle(handle);
        return expected_mutex ? 3 : 0;
    }
    /* ObjectTypeInformation=2; first field is UNICODE_STRING. No object-name query. */
    status = query(handle, 2, buffer.bytes, sizeof(buffer.bytes), &needed);
    if (status >= 0) {
        UNICODE_STRING *type = (UNICODE_STRING *)buffer.bytes;
        printf("object_type=%.*ls\n", (int)(type->Length / sizeof(WCHAR)), type->Buffer);
        type_is_mutex = type->Length == 6 * sizeof(WCHAR) && !wcsncmp(type->Buffer, L"Mutant", 6);
    } else {
        printf("object_type=query_failed ntstatus=0x%08lx needed=%lu\n",
               (unsigned long)status,
               (unsigned long)needed);
    }
    if (control)
        CloseHandle(handle);
    return expected_mutex && !type_is_mutex ? 3 : 0;
}
