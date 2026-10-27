/** @file inspect_probe_worker.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent descendant fixture retaining inherited probe output handles.
 */
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <unistd.h>
#endif

int main(void) {
    char path[4096];
    const char *root = getenv("GOAT_PROBE_TEST_MARKERS");
    if (!root || snprintf(path, sizeof(path), "%s/descendant", root) >= (int)sizeof(path))
        return 90;
    FILE *file = fopen(path, "wb");
#ifdef _WIN32
    unsigned long pid = GetCurrentProcessId();
#else
    unsigned long pid = (unsigned long)getpid();
#endif
    if (!file || fprintf(file, "%lu\n", pid) < 0 || fclose(file))
        return 91;
#ifdef _WIN32
    Sleep(INFINITE);
#else
    for (;;)
        pause();
#endif
    return 0;
}
