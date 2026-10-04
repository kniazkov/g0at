/** @file compiler.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Compiler substitutes for invalid and unrelated artifacts.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *output = NULL;
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "-o"))
            output = argv[i + 1];
    if (!output)
        return 1;
    FILE *target = fopen(output, "wb");
    if (!target)
        return 2;
    if (strstr(argv[0], "invalid-compiler")) {
        fputs("not a shared library", target);
    } else {
        const char *path = getenv("GOAT_PIPELINE_WRONG_LIBRARY");
        FILE *source = path ? fopen(path, "rb") : NULL;
        if (!source) {
            fclose(target);
            return 3;
        }
        int ch;
        while ((ch = fgetc(source)) != EOF)
            fputc(ch, target);
        fclose(source);
    }
    return fclose(target) ? 4 : 0;
}
