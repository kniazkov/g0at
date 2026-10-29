/** @file context_retarget_compiler.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Compiler stand-in retargeting a source alias before native fallback.
 */
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#endif
int main(void) {
    const char *alias = getenv("GOAT_TEST_ALIAS");
    const char *swapped = getenv("GOAT_TEST_SWAPPED");
    const char *marker = getenv("GOAT_TEST_MARKER");
    if (!alias || !swapped || !marker)
        return 2;
#ifdef _WIN32
    /* Move the reparse-point entry itself, without CRT-specific link handling.
       These fixture paths are ASCII; production Unicode paths use the fs API. */
    if (!DeleteFileA(alias) || !MoveFileExA(swapped, alias, 0))
        return 2;
#else
    if (remove(alias) || rename(swapped, alias))
        return 2;
#endif
    FILE *file = fopen(marker, "wb");
    if (!file)
        return 3;
    fputs("retargeted", file);
    fclose(file);
    return 1;
}
