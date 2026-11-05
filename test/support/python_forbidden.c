/** @file python_forbidden.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Native fail-closed sentinel for the scoped Python-free CI command path.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <wchar.h>
#    include <windows.h>
#else
#    include <unistd.h>
#endif

/* The independent marker does not rely on inherited environment. Even a caller
 * that clears the environment or ignores a logging error leaves this evidence. */
static FILE *open_marker(void) {
#ifdef _WIN32
    wchar_t path[32768];
    DWORD length = GetModuleFileNameW(NULL, path, sizeof(path) / sizeof(path[0]));
    if (!length || length >= sizeof(path) / sizeof(path[0]))
        return NULL;
    wchar_t *slash = wcsrchr(path, L'\\');
    if (!slash || (size_t)(slash - path) + 10 >= sizeof(path) / sizeof(path[0]))
        return NULL;
    wcscpy(slash + 1, L"invoked");
    return _wfopen(path, L"ab");
#else
    char path[32768];
    ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length <= 0 || (size_t)length >= sizeof(path) - 1)
        return NULL;
    path[length] = 0;
    char *slash = strrchr(path, '/');
    if (!slash || (size_t)(slash - path) + 10 >= sizeof(path))
        return NULL;
    strcpy(slash + 1, "invoked");
    return fopen(path, "ab");
#endif
}

int main(int argc, char **argv) {
    FILE *marker = open_marker();
    int failed = !marker;
    if (marker) {
        if (fputs("Forbidden interpreter invocation\n", marker) < 0)
            failed = 1;
        if (fclose(marker))
            failed = 1;
    }
#ifdef _WIN32
    const wchar_t *path = _wgetenv(L"GOAT_NO_PYTHON_LOG");
    FILE *log = path ? _wfopen(path, L"ab") : NULL;
#else
    const char *path = getenv("GOAT_NO_PYTHON_LOG");
    FILE *log = path ? fopen(path, "ab") : NULL;
#endif
    if (!log) {
        fputs("Python-free guard cannot record forbidden invocation details\n", stderr);
        return 98;
    }
    fputs("Forbidden interpreter invocation:", log);
    for (int i = 0; i < argc; ++i) {
        fputc(' ', log);
        for (const unsigned char *s = (const unsigned char *)argv[i]; *s; ++s) {
            if (*s < 32 || *s == 127)
                fprintf(log, "\\x%02x", *s);
            else
                fputc(*s, log);
        }
    }
    fputc('\n', log);
    if (ferror(log))
        failed = 1;
    if (fclose(log))
        failed = 1;
    fputs("Python is forbidden in this project command path\n", stderr);
    return failed ? 98 : 99;
}
